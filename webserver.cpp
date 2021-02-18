#include "webserver.h"

WebServer::WebServer () {
    // http_conn 类对象
    user = new http_conn[MAX_FD];
    
    // root 文件夹路径
    char server_path[200];
    // getcwd()会将当前的工作目录绝对路径复制到参数buf 所指的内存空间，参数size 为buf 的空间大小。
    getcwd(server_path, 200); 
    string root = "/root";
    m_root = (char *)malloc(strlen(server_path) + root.size() + 1); // 开辟存放文件夹路径的空间
    //strcpy(m_root, server_path); // strcpy（）容易发生内存溢出
    strncpy(m_root, server_path, strlen(server_path));
    strcat(m_root, root); // 拼接

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    // 关闭文件描述符
    close (m_epollfd);
    close (m_listenfd);
    close (m_pipefd[1]);
    close (m_pipefd[0]);
    // 防止内存泄漏
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init (int port, string user, string passWord, string databaseName,
                    int log_write, int opt_linger, int trig_mode, int sql_num, int thread_num,
                    int close_log, int actor_model) {
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trig_mode;
    m_close_log = close_log;
    m_actormodel = actor_model; 
}

void WebServer::trig_mode() {
    // LT + LT
    if (0 == m_TRIGMode) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0；
    }
    // ET + ET
    else if (3 == m_TRIGMode) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write () {
    if (0 == m_close_log) {
        // 日志初始化
        if (1 == m_log_write) {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        }
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        
    }
}

void WebServer::sql_pool() {
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance(); // 获取数据库连接实例，因为数据库连接池采用的是单例模式保证安全性
    m_connPool->init("localhst", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initmysql_result(m_connPool); // users 是连接对象
}

void WebServer::thread_pool () {
    // 线程池
    m_pool = new threadpool<http_conn> (m_actormodel, m_connPool, m_thread_num);
}

// 监听
void WebServer::eventListen () {
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0); // 建立监听文件描述符
    assert(m_listenfd >= 0); // 断言，如果监听文件描述符小于0则退出

    // 优雅关闭连接
    if (0 == m_OPT_LINGER) {
        struct linger tmp = {0, 1};
        setsockopt (m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER) {
        struct linger tmp = {1, 1};
        setsocketopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero (&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt (m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address)); // 绑定文件描述符
    assert (ret >= 0);
    ret = listen (m_listenfd, 5);
    assert (ret >= 0);

    utils.init (TIMESLOT); // 定时器初始化

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER]; // 内核事件
    m_epollfd = epoll_create(5); // 创建epoll监听文件描述符
    assert (m_epollfd != -1);

    utils.addfd (m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair (PF_UNIX, SOCK_STREAM, 0, m_pipefd); // 创建文件描述符对，成功返回0，失败返回-1
    assert (ret != -1);
    utils.setnonblocking (m_pipefd[1]);
    utils.addfd (m_epollfd, m_pipefd[0], false, 0);

    utils.addsig (SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm (TIMESLOT);

    // 工具类，信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer (int connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName) {
    user[connfd].init (connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    // 初始化client_data 数据
    // 创建定时器， 设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    user_timer[connfd].address = client_address;
    user_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func; // 回调函数，用于处理非活跃连接
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT; // 设置定时器超时时间
    users_timer[connfd].timer = timer; // 更新超时定时器
    utils.m_timer_lst.add_timer (timer); // 将定时器添加到定时器链表中
}

// 若有数据传输，则将定时器往后延3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer (util_timer *timer) {
    timer_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO ("%s", "adjust timer once");
}

void WebServer::deal_timer (util_timer *timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if (timer) {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO ("close fd %", users_timer[sockfd].sockfd);
}

bool WebServer::dealclinetdata () {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof (client_address);
    if (connfd < 0) {
        int connfd = accept (m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0) {
            LOG_ERROR ("%s:errno is :%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) {
            utils.show_error (connfd, "Internal server busy");
            LOG_ERROR ("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    else {
        while (1) {
            int connfd = accept (m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0) {
                LOG_ERROR ("%s:errno is : %d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD) {
                utils.show_error (connfd, "Internal server busy");
                LOG_ERROR ("%s", "Internal server busy");
                break;
            }
            timer (connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal (bool &timeout, bool &stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1) {
        return false;
    }
    else if (ret == 0) {
        return false;
    }
    else {
        for (int i = 0; i < ret; ++i) {
            switch (signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

