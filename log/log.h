//通过局部变量的懒汉单例模式创建日志实例，对其进行初始化生成日志文件后，格式化输出内容，并根据不同的写入方式，完成对应逻辑，写入日志文件。
//私有化它的构造函数，以防止外界创建单例类的对象；使用类的私有静态指针变量指向类的唯一实例，并用一个公有的静态方法获取该实例。
#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

class Log{
private:
    Log();//->创建的时候不初始化，用的时候再初始化
    virtual ~Log();

    // 消息队列的写入
    void *async_write_log(){
        string single_log;//储存单条日志
        while(m_log_queue->pop(single_log)){//从阻塞队列中不断的取日志，放进single_log中
            m_mutex.lock();
            fputs(single_log.c_str(),m_fp);//将日志写入文件。single_log.c_str() 将 C++ 字符串转换为 C 风格字符串，m_fp 文件指针。
            m_mutex.unlock();
        }
    }
private:
    char dir_name[128]; //路径名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针

    char log_name[128]; //log文件名
    char *m_buf;        //要输出的内容
    block_queue<string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    locker m_mutex;

    int m_close_log; //关闭日志
public:

    // 公有的实例获取方法
    static Log  *get_instance(){//C++11后局部变量懒汉不同加锁
        static Log instance;
        return &instance;
    }

    //异步写入日志  一个单独的写线程，持续处理日志写入操作
    static void *flush_log_thread(void *args){
        Log::get_instance()->async_write_log();
    }

    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);
};

//通过m_close_log全局开关控制日志输出  每次写入后调用flush保证及时落盘（可能影响性能）

// 增加级别参数检查
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif