#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>

#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool () {
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance () {
    static connection_pool connPool;
    return &connPool;
}

// 
void connection_pool::init (string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log) {
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    for (int i = 0; i < MaxConn; ++i) {
        MYSQL *con = NULL;
        con = mysql_init(con); // 初始化数据库连接

        if (con == NULL) {
            LOG_ERROR ("MySQL Error");
            exit(1);
        }

        con = mysql_real_connect (con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0); //与数据库建立连接

        if (con == nullptr) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        connList.push_back(con); // 若成功建立连接，将该链接插入数据库连接池
        ++m_FreeConn;// 为啥是空闲连接数增加？******************这是初始化过程，存放在数据库中的连接都是空闲连接（还没有使用）

    }

    reserve = sem (m_FreeConn);

    m_MaxConn = m_FreeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接
MYSQL *connection_pool::GetConnection() {
    MYSQL *con = NULL;

    if (connList.size() == 0) {
        return nullptr;
    }

    reserve.wait(); // 等待事件发生

    lock.lock(); // 加锁

    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;
}

// 释放当前使用连接
bool connection_pool::ReleaseConnection(MYSQL *con) {
    if (con == nullptr) {
        return false;
    }

    lock.lock();

    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    reserve.post(); // 发信号
    return true;
}

// 销毁数据库
void connection_pool::DestroyPool() {
    lock.lock();
    if (connList.size() > 0) {
        for (auto it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *con = *it;
            mysql_close(con);
        }

        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }

    lock.unlock();
}

// 当前空闲连接数
int connection_poo::GetFreeConn() {
    return this->m_FreeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
    pollRAII->ReleaseConnection(conRAII);
}