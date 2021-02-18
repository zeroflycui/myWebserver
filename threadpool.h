#idndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"
#include "CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool {
public:
    threadpool (int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool ();

    bool append (T *request, int state);
    bool append_p (T *request);

private:
    // 工作线程中运行函数，不断从工作队列中取出任务并执行
    static void *worker (void *arg);
    void run();

private:
    int m_thread_number; // 线程池中的线程数
    int m_max_requests; // 请求队列中允许的最大请求数
    pthread_t *m_threads; //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; // 请求队列
    locker m_queuelocker;       // 保护请求队列的互斥锁
    sem m_queuestat;           // 是否有任务需要处理， 通过信号量来表示请求队列是否有任务到来
    connection_pool *m_connPool; // 数据库
    int m_actor_model;   // 模型切换

};

template<typename T>
threadpool<T>::threadpool (int actor_model, connection_pool *connPool, int thread_number, int max_requests) : 
                           m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool) {
    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number]; // 创建线程池（数组）
    if (!m_threads) {
        throw std::exception();
    }
    // 遍历线程池，创建worker线程，若创建失败则删除线程池
    for (int i = 0; i < thread_number; ++i) {
        if (pthread_create (m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) { // 创建子线程， 成功则返回0，失败返回非零值
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

// 工作队列中添加请求
template<typename T>
bool threadpool<T>::append (T *request, int state) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }

    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//
template<typename T>
bool threadpool<T>::append_p (T *request) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back (request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//
template<typename T>
void *threadpool<T>::worker (void *arg) {
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

//
template<typename T>
void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }
        if (m_actor_mode == 1) {
            if (request->m_state == 0) {
                if (request->read_once()) {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else {
                if (request->wirte()) {
                    request->improv = 1;
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else {
            connectionRAII mysqlcon (&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif