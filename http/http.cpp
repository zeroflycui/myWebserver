#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy. \n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Inernal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

// 初始化数据库连接池
void http_conn::initmysql_result (connection_pool *connPool) {
    // 先从连接池中取出一个连接
    MYSQL *mysql = nullptr;
    connectionRAII mysqlcon (&mysql, connPool); //使用RAII机制控制数据库连接池的连接与释放

    // 从user表中检索username， passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username, passwd FROM user")) {
        LOG_ERROR ("SELECT error :%s\n", mysql_error(mysql));
    }

    //从表中检索出完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    //返回所有字段结构的数组
    MYSQL_FIFLD *fields = mysql_fetch_fields(result);
    // 从结果集中获取下一行，将对应的用户名喝密码存入map中
    while (MYSQL_ROW row = mysql_fech_row (result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }

}

// 对文件描述符设置非阻塞
int setnonblocking (int fd) {
    int old_option = fcntl (fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl (fd, F_SETFL, new_option);
    return old_option;
}

// 在内核事件表中注册读事件
void addfd (int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    // 选择触发的模式
    if (TRIGMode == 1) 
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
    {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    // 选择是否开启oneshot 模式
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl (epollfd, EPOLL_CTL_ADD, fd, &event); // 向epoll内核表中添加，epoll事件描述符
    setnonblocking (fd); // 设置为非阻塞
}

//从内核事件表中删除描述符
void removefd (int epollfd, int fd) {
    epoll_ctl (epollfd, EPOLL_CTL_DEL, fd, 0);
    close (fd); // 关闭文件描述符
}

// 修改epoll内核事件中的模式
void modfd (int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1) {
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    }
    else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }

    epoll_ctl (epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn (bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf ("close %d\n", m_sockfd);
        removefd (m_epollfd, m_sockfd); // 移除内核事件表中的文件描述符
        m_sockfd = -1;
        m_user_count--; // 以用连接数减一
    }
}
// 初始化连接，外部初始化套接字地址
void http_conn::init (int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;

    addfd (m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++; // 已用连接数加一

    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    // .c_str() 将string类型的转化为c类型的字符串类型
    strcpy (sql_user, user.c_str());
    strcpy (sql_passwd, passwd.c_str());
    strcpy (sql_name, sqlname.c_str());

    init();
}

// 初始化新接受的连接
void http_conn::init () {
    musql = nullptr;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    // memset (s1, s2, n) 将s1中当前位置后的n个字符用s2代替
    memset (m_read_buf, '\0', READ_BUFFER_SIZE);
    memset (m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset (m_real_file, '\0', FILENAME_LEN);
}

http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    // m_checked_idx 表示已经从读缓冲区中当前读取的位置
    // m_read_idx 表示报文的末尾的下一个位置
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx]; // 当前读取的读缓冲区中的报文内容
        if (temp == '\r') { // 若是回车字符
            // 假如下一个位置是报文的最后一个字符
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n') 
            {
                m_read_buf [m_checked_idx++] = '\0';
                m_read_buf [m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n') {
            if （m_checked_idx > 1 && m_read_buf [m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 如果没有读到\r或\n，说明没有读取一行完整的报文，则继续接收
    return LINE_OPEN;
}
// 读取客户端的请求数据
bool http_conn::read_once () {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    // LT模式下读取数据
    if (m_TRIGMode == 0) {
        //recv(socket s, char *buf, int len, int flag) 接收指定套接字s的报文，存放到长度为len的buf容器中，flag通常设置为0
        // 成功返回接收的字节数，失败返回-1（但是会有对应的错误提示如EAGAIN等；返回0说明另一端关闭
        bytes_read = recv (m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if （bytes_read <= 0)
            return false;

        return true;
    }
    // ET模式下读取数据，需要循环读取, 直到遇到EAGAIN停止
    else {
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

http_conn::HTTP_CODE http_conn::parse_request_line (char *text) {
    m_url = strpbrk (text, " \t"); // 获得字符串text第一次出现空格或'\t'的位置
    if (!m_url)
        return BAD_REQUEST;
    
    *m_url++ = '\0'; // 给请求方式做截断
    // 解析报文的请求方式
    char *method = text;
    if (strcasecmp (method, "GET") == 0) // strcasecmp(s1, s2) 比较s1和S2相等的字符串，若相同则返回0；
    {
        cout << "GET" << endl;
        m_method = GET;
    }
    else if (strcasecmp (method, "POST") == 0) {
        cout << "POST" << endl;
        m_method = POST;
        cgi = 1;
    }
    else
    {
        return BAD_REQUEST;
    }
    
    m_url += strspn(m_url, " \t"); // 保证在判断为请求方式之后的没有" \t"
    m_version = strpbrk (m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn (m_version, " \t");

    if (strncasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if (strcasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/'); // 字符串m_url中第一次出现 '/'包括'/'之后的字符串
        // m_url 指的是/...后的内容
    }

    if (strcasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    // 一般不会加上述两种符号,直接是单独的资源连接或者/+访问链接
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    //当请求资源的长度仅为1时，也就是m_url = '/'， 则跳转到判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    // 请求行处理完毕，将主状态机转移处理请求状态
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;

}
// 解析请求头和空行
http_conn::HTTP_CODE http_conn::parse_headers (char *text) {
    if (text[0] == '\0') {
        // 判断请求消息体是否为空，若不为空则变换主状态机的状态
        if (m_countent_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    // 判断请求头中是否含有保持长连接
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
            m_linger = true; 
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn (text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        LOG_INFO ("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
// 解析报文
http_conn::HTTP_CODE http_conn::process_read () {
    LINE_STATUS line_status = LINE_OK;
    char *text = 0;
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line())) == LINE_OK) {
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INIERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
//
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy (m_real_file, doc_root);
    int len = strlen(doc_root); // doc_root存放响应请求的文件路径
    const char *p = strrchr (m_url, '/'); // 找到m_url中的/位置

    // 处理cgi, 只有登录和注册时需要开启cgi，进行校验
    if (cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')) {
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200); // 为url分配内存
        strcpy (m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy (m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free (m_url_real); // 释放内存

        // 从请求资源中提取出用户名和密码
        char name[100], password[100];
        int i = 0;
        
        // m_string 存放请求行内容 例如 user=123&passwd=root
        for (i = 5; m_string[i] != '&'; ++i) {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0'; // 在用户名末尾添加终止符

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j) {
            password[j] = m_string[i];
        }
        password[j] = '\0';

        // 注册
        if (*(p + 1) == '3') {
            //构造插入用户名和密码的SQL语句 "INSERT INTO user(username, passwd) VALUES('name', 'passwd')"
            char *sql_insert = (char *)malloc(sizeof(char) *200);
            strcpy (sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat (sql_insert, "'");
            strcat(sql_insert, name);
            strcat (sql_insert, "', '");
            strcat (sql_insert, passwd);
            strcat (sql_insert, "')");

            //检查用户名是否已已经在map容器中
            if (users.find(name) == users.end()) {

                m_lock.lock(); // 查询的过程中需要上锁，防止数据写入
                int res = mysql_query (mysql, sql_insert); // SQL语句查询
                users.insert (pair<string, string> (name, password)); // 插入到map容器中
                m_lock.unlock();

                if (!res) 
                    strcpy (m_url, "/log.html");
                else
                {
                    strcpy (m_url, "/registerError.html");
                }
                
            }
            else
            {
                strcpy (m_url, "/registerError.html");
            }
            
        }
        // 若为登录
        else if (*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password) {
                strcpy (m_url, "/welcome.html");
            }
            else {
                strcpy (m_url, "/logError.html");
            }
        }
        free (sql_insert);/////////////////////////////////////////////////////////////////////////////////////
    }

    // 跳转注册界面
    if (*(p + 1) == '0') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy (m_url_real, "/register.html");
        strncpy (m_real_file + len, m_url_real, strlen(m_url_real));

        free (m_url_real);
    }
    else if (*(p + 1) == '1') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy (m_url_real, "/log.html");
        strncpy (m_real_file + len, m_url_real, strlen(m_url_real));

        free (m_url_real);
    }
    else if (*(p + 1) == '5') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy (m_url_real, "/picture.html");
        strncpy (m_real_file + len, m_url_real, strlen(m_url_real));

        free (m_url_real);
    }
    else if (*(p + 1) == '6') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy (m_url_real, "/video.html");
        strncpy (m_real_file + len, m_url_real, strlen(m_url_real));

        free (m_url_real);
    }
    else if (*(p + 1) == '7') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy (m_url_real, "/fans.html");
        strncpy (m_real_file + len, m_url_real, strlen(m_url_real));

        free (m_url_real);
    }
    else {
        strncpy (m_real_file + len, m_url, FILENAME_LEN - LEN - 1);
    }
    // 请求文件资源不存在
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    // 判断文件权限，是否可读，不可读返回FORBIDDEN_REQUEST
    if (!(m_file_stat.st_mode & S_IROTH)) 
        return FORBIDDEN_REQUEST;
    
    //判断文件类型，如是目录返回错误
    if(S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    
    int fd = open(m_real_file, O_RDONLY) // 以只读的方式读取文件
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // 将文件映射到内存中
    close (fd);
    return FILE_REQUEST;
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap (m_file_address, m_file_stat.st_size); // 关闭文件属性所指向的映射内存起始地址
        m_file_address = 0;
    }
}

// 将响应报文发送个客户端
bool http_conn::write() {
    int temp = 0;
    // 要发送的数据为空
    if (bytes_to_send == 0) {
        modfd (m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1) {
        // 以聚集写的方式将数据全部发送给客户端
        temp = writev (m_sockfd, m_iv, m_iv_count); // 成功返回传送的字节，失败返回小于0
        // 单次没有发送成功
        if (temp < 0) {
            // 若是由于EAGAIN问题，则是因为写缓冲区已满，重修改文件描述符
            if (errno == EAGAIN) {
                modfd (m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            // 否则是由于其他原因，取消mmap映射
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0) {
            unmap();
            modfd (m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            if (m_linger) {
                init(); // 若是长连接，需要重新初始化
                return true;
            }
            else {
                return false;
            }
        }
    }
}
// 是否成功添加响应
bool http_conn::add_response (const char *format, ...) { // 可变参数列表，按照参数列表的第一个为格式化标准，后面为格式化目标参数
    // 写入内容超过缓冲区
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }

    va_list arg_list; // 可变参数的信息
    va_start (arg_list, format); // 以format格式初始化参数列表
    // vsnprintf(char *str, size_t size, const char* format, va_list ap); 将可变参数ap按照format格式化输出到一个数组str
    // str是输出的数组；size指定大小防止越界；format格式化数组；ap可变参数列表
    // 返回值：为负数表示没有完全写入；返回非负数且小于n的才算成功；返回值为成功写入的字符个数
    int len = vsnprintf (m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list); //写入剩余写缓冲区
    // 如果写入失败，返回
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list); // 结束参数列表
        return false;
    }

    //更新写缓存的指针
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

// 添加状态行信息
bool http_conn::add_status_line (int status, const char *title) { 
    return add_response ("%s %d %s\r\n", "HTTP/1.1", status, title); //添加http版本号，状态码，状态码的文本描述
}

//添加响应头，包括文本长度，是否长连接，是否添加空行
bool http_conn::add_headers (int content_len) {
    return add_content_length (content_len) && add_linger() && add_blank_line();
}

// 
bool http_conn::add_content_length (int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}

//
bool http_conn::add_content_type() {
    return add_response ("Content-Type:%s\r\n", "text/html");
}

//
bool http_conn::add_linger() {
    return add_response ("Connection:%s\r\n", (m_linger=true) ? "keep-alive" : "close");
}

//
bool http_conn::add_blank_line () {
    return add_response ("%s", "\r\n");
}

// 添加响应消息内容
bool http_conn::add_content (const char *content) {
    return add_response("%s", content);
}

// 根据http的响应状态，添加对应的响应报文信息
bool http_conn::process_write (HTTP_CODE ret) {
    switch (ret) {
        // 内部错误 500
        case INTERNAL_ERROR: {
            add_status_line (500, error_500_title);
            add_headers (strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        //错误请求 404
        case BAD_REQUEST: {
            add_status_line (404, error_404_title);
            add_headers (strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        // 禁止访问 403
        case FORBIDDEN_REQUEST: {
            add_status_line (403, error_403_title);
            add_headers (strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        //请求成功
        case FILE_REQUEST: {
            add_status_line (200, ok_200_title);
            // 如果请求文件不为空
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else {
                const char *ok_string = "<html><body></body></html>";
                add_headers (strlen(ok_string));
                if (!add_content(ok_string)) {
                    return false;
                }
            }
        }
        default:
            return false;
    }
    // 只有请求完文件才涉及到指向指向文件地址的变化，其他请求只考虑写缓冲区的变化
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd (m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write (read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}