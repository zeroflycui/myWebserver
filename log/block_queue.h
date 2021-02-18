// 循环数组实现阻塞队列
#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#include "../locker.h"
using namespace std;

template<class T>
class block_queue {
public:
    block_queue (int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);
        }

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear () {
        // 清除前加锁
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock(); // 清除后解锁
    }

    ~block_queue () {
        m_mutex.lock();
        if (m_array != NULL) {
            delete [] m_marray; // 释放申请的资源
        }
        m_mutex.unlock();
    }

    // 判断阻塞队列是否满
    bool full () {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 判断队列是否为空
    bool empty() {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队首元素
    bool front (T &value) {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }

    // 返回队尾元素
    bool back (T &value) {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    // 返回队列中的元素个数
    int size() {
        int tmp = 0;

        m_mutex.lock();
        tem = m_size;
        m_mutex.unlock();

        return tep;
    }

    //
    int max_size () {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();

        return tmp;
    }

    /*
    * 再阻塞队列中添加内容时首先要先 所有使用队列的线程先唤醒
    * 当有元素push进队列，相当于生产者生产一个元素
    * 若当前没有线程等待条件变量，则唤醒无意义
    */
   bool push (const T &item) {
       m_mutex.lock();
       if (m_size >= m_max_size) {
           m_cond.broadcast();
           m_mutex.unlock();
           return false;
       }

        // 尾部加入新元素
       m_back = (m_back + 1) % m_max_size; // 确定新加入元素的下标
       m_array[m_back] = item; // 更新数组中的元素
       m_size++;
       m_cond.broadcast(); // 广播唤醒
       m_mutex.unlock();
       return true;
   }

    // 删除时若队列为空则会等待条件变量
   bool pop (T &item) {
       m_mutex.lock();

       //当前队列为空
       while (m_size <= 0) {
           // 且没有等待条件变量
           if (!m_cond.wait(m_mutex.get())) {
               m_mutex.unlock();
               return false;
           }
       }

        // 删除头部元素, 通过移动头部下标指针实现删除头部元素
       m_front = (m_front + 1) % m_max_size;
       item = m_array[m_front];
       m_size--;

       m_mutex.unlock();
       return true;
   }

   // 增加了超时处理
   bool pop (T &item, int ms_timeout) {
       struct timespec t = {0,0};
       struct timeval now = {0,0};
       gettimeofday (&now, NULL);
       m_mutex.lock();
       if (m_size <= 0) {
           t.tv_sec = now.tv_sec + ms_timeout / 1000;
           t.tv_nsec = (ms_timeout % 1000) * 1000;
           if (!m_cond.timewait(m_mutex.get(), t)) {
               m_mutex.unlock();
               return false;
           }
       }

       if (m_size <= 0) {
           m_mutex.unlock();
           return false;
       }

       m_front = (m_front + 1) % m_max_size;
       item = m_array[m_front];
       m_size--;
       m_mutex.unlock();
       return true;
   }

private:
    locker m_mutex;
    cond m_cond;

    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;

};

#endif