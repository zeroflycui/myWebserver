#ifndef _CONNECTION_POOL
#define _CONNECTION_POOL

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>

#include "locker.h"
#include "/log/log.h"

using namespace std;

class connection_pool {
public:
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL *conn);
    int GetFreeConn();
    void DestroyPool();

    // 单例模式
    static connection_pool *GetInstance();

    void init (string url, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);
private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn; //最大连接数
    int m_CurConn; //当前已使用的连接数
    int m_FreeConn; // 空闲的连接数
    locker lock;
    list<MYSQL *> connList; // 连接池
    sem reserve;

public:
    string m_url; // 主机地址
    string m_Port; // 数据库端口号
    string m_User; // 登录数据库的名字
    string m_PassWord; // 登录密码
    string m_DatabaseName; // 使用数据库的名字
    int m_close_log;        // 日志开关
};

class connectionRAII {
public:
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};

#endif
