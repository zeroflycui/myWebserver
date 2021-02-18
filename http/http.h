#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048; // 读缓存大小2G
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓存区大小1G

    // 枚举报文请求的类型
    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 枚举主状态机的状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    // 枚举http连接的状态
    enum HTTP_CODE {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    //枚举从状态机的状态
    enum LINE_STATUS {
        LINE_OK = 0, // 读取一行完整的请求报文
        LINE_BAD,    // 读取请求报文失败
        LINE_OPEN    // 没有读取完一行完整的报文
    };
public:
    http_conn () {}
    ~http_conn () {}

public:
    // 初始化套接字地址，函数内部会调用私有方法init
    void init (int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    // 关闭连接
    void close_conn (bool real_colse = true);
    // 处理
    void process();
    // 读取请求报文
    bool read_once();
    // 响应
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    // 同步线程初始化数据库读取表
    void initmysql_result (connection_pool *connPool);

    int timer_flag;
    int improv;

private:
    void init();
    HTTP_CODE process_read();
    bool process_write (HTTP_CODE ret);
    HTTP_CODE parse_request_line (char *test); // 解析请求行
    HTTP_CODE parse_headers (char *text); // 解析请求头
    HTTP_CODE parse_content (char *text); // 解析消息体
    HTTP_CODE do_request();
    char *get_line() {return m_read_buf + m_start_line;};
    LINE_STATUS parse_line();
    void unmap ();
    bool add_response (const char *format, ...);
    bool add_content (const char *content); // 
    bool add_status_line (int status, const char *title); // 添加状态行
    bool add_headers (int content_length); // 添加响应头
    bool add_content_type(); // 报文的类型
    bool add_content_length (int content_length); // 报文的长度
    bool add_linger();     // 设置连接是否持续
    bool add_blank_line(); // 添加空行

public:
    static int m_epollfd; 
    static int m_user_count;
    MYSQL *mysql;
    int m_state; // 读为0，写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区设置为字符型数组
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE]; //写缓冲区设置为字符型数组
    int m_write_idx;
    CHECK_STATE m_check_state;
    METHOD m_method;
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi; // 是否启用POST
    char *m_string; // 存储请求头数据
    int bytes_to_send; // 还没有发送的
    int bytes_have_send; // 已经发送
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif