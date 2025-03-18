#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"


//主要有两个部分组成：
//1.线程池管理模块，包括创建线程，销毁线程，分配任务
//2.工作线程模块，线程池管理模块创建线程后，线程就会从工作队列中取出任务并执行
//线程池管理模块和工作线程模块是通过一个工作队列来实现的
//工作队列是一个任务队列，由工作线程从工作队列中取出任务并执行
//线程池管理模块和工作线程模块通过一个互斥锁来保护工作队列，防止多个线程同时访问工作队列

template<typename T>

class threadpool{
private:
    pthread_t * m_threads;//线程池数组，大小为m_thread_number
    int m_thread_number;//线程池中的线程数

    std::list<T *> m_workqueue;//请求队列
    int m_max_requests;//请求队列中允许的最大请求数
    locker m_queuelocker;//保护请求队列的互斥锁 ->写入请求队列
    sem m_queuestat;//是否有任务需要处理 -> 从请求队列读
    int m_actor_model;          //模型切换

    connection_pool * m_connPool;//数据库连接池  数据库连接的内容是为了配合工作线程的读事件，为了本项目而引入的

public:
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();

    //将请求加入请求队列
    bool append_p(T * request);
    bool append(T *request, int state);
private:
    //工作线程运行的函数，它不断从工作队列中取出任务并执行之
    static void * worker(void * arg);

    void run();
};

template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if(thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) throw std::exception();

    //线程id初始化
    for(int i = 0; i < thread_number; ++i) {
        //创建线程，成功返回0，失败返回错误号  将worker函数设置为线程函数,this指针作为参数传入
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        //将线程分离，使得线程结束时自动释放资源  成功返回0，失败返回错误号
        if(pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}


template<typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template<typename T>
bool threadpool<T>::append_p(T * request) {
    //操作工作队列时一定要加锁，因为它被所有线程共享
    m_queuelocker.lock();

    //请求队列满了，解锁并返回false
    if(m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    
    //请求队列未满，将请求加入请求队列
    m_workqueue.push_back(request);
    m_queuelocker.unlock();

    //信号量加1
    m_queuestat.post();
    return true;
}

template<typename T>
bool threadpool<T>::append(T * request,int state) {
    //操作工作队列时一定要加锁，因为它被所有线程共享
    m_queuelocker.lock();

    //请求队列满了，解锁并返回false
    if(m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }

    request->m_state = state;
    
    //请求队列未满，将请求加入请求队列
    m_workqueue.push_back(request);
    m_queuelocker.unlock();

    //信号量加1
    m_queuestat.post();
    return true;
}

template<typename T>
void * threadpool<T>::worker(void * arg) {

    //将参数强转为threadpool类型，调用成员函数
    threadpool * pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait(); // 等待信号量,有任务时唤醒
        m_queuelocker.lock(); // 加锁,保护工作队列
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock(); // 队列为空,解锁并继续等待
            continue;
        }
        T *request = m_workqueue.front(); // 获取队列首部的请求
        m_workqueue.pop_front(); // 移除已处理的请求
        m_queuelocker.unlock(); // 解锁

        
        if (!request)
            continue; // 无效请求,继续下一个循环

        if (1 == m_actor_model) // Reactor模式
        {
            if (0 == request->m_state) // 读事件
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool); // 获取数据库连接
                    request->process(); // 处理请求
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else // 写事件
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else // Proactor模式
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool); // 获取数据库连接
            request->process(); // 处理请求
        }
    }
}
#endif