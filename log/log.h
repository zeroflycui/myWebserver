#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>

#include "block_queue.h"

using namespace std;

class lgo {
public:

    // c++11 后，使用局部懒汉不用加锁
    static Log *get_instance() {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread (void *args) {
        Log::get_instance()->async_write_log();
    }

    // 可选择地参数有日志文件、日志缓冲区大小、最大行数以及最长日志队列
    bool init (const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log (int level, const char *format, ...);

    void flush (void); // 刷新缓冲区

private:
    Log();
    virtual ~Log(); // 虚析构函数
    void *async_write_log() {
        string single_log;
        // 从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs (single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; // 路径名
    char log_name[128]; // log文件名
    int m_split_lines; // 日志最大行数
    int m_log_buf_size; // 日志缓冲区大小
    long long m_count; // 日志行数记录
    int m_today;       // 因为按天分类，记录当前时间是哪一天
    FILE *m_fp;        // 打开log文件地指针
    char *m_buf;       // 
    block_queue<string> *m_log_queue; // 阻塞队列
    bool m_is_async;
    locker m_mutex;
    int m_close_log; 
};

#define LOG_DEBUG (format, ...) if (m_close_log == 0) { Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO (format, ...) if (m_close_log == 0) { Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN (format, ...) if (m_close_log == 0) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR (format, ...) if (m_close_log == 0) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif