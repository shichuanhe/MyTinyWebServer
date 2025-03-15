//·异步写入方式，将生产者-消费者模型封装为阻塞队列，创建一个写线程，工作线程将要写的内容push进队列，写线程从队列中取出内容，写入日志文件。

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>  
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>  
#include "../lock/locker.h"
using namespace std;

template <class T>
class block_queue
{
public:
    //初始化
    block_queue(int max_size=1000){
        if(max_size<0) throw std::invalid_argument("max_size must be positive");
        
        m_max_size=max_size;
        m_array=new T[max_size];
        m_size=0;
        m_front=-1;
        m_back=-1;
    }
    //析构  删除*m_array
    ~block_queue(){
        m_mutex.lock();
        if(m_array!=NULL) delete[] m_array;
        m_mutex.unlock();
    }

    //清空队列 ->元素不用删 覆盖就行
    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    //判断是否满队列
    bool full(){
        m_mutex.lock();
        if(m_size>=m_max_size){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    //判断是否为空
    bool empty(){
        m_mutex.lock();

        if(m_size==0){
            m_mutex.unlock();
            return true;
        }

        m_mutex.unlock();
        return false;
    }

    //返回队首元素
    bool front(T &value){
        m_mutex.lock();
        if(m_size==0){
            m_mutex.unlock();
            return false;
        }
        value=m_array[m_front];
        m_mutex.unlock();
        return true;
    }

    //返回队尾元素
    bool back(T &value){
        m_mutex.lock();
        if(m_size==0){
            m_mutex.unlock();
            return false;
        }
        value=m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    //返回队列现有元素个数
    int size(){
        int tmp=0;

        m_mutex.lock();
        tmp=m_size;
        m_mutex.unlock();

        return tmp;
    }

    //返回队列最大容量
    int max_size(){
        int tmp=0;

        m_mutex.lock();
        tmp=m_max_size;
        m_mutex.unlock();

        return tmp;
    }

    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item){
        m_mutex.lock();

        if(m_size>=m_max_size){ //如果满队列了，通知消费者使用 返回false
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        m_back=(m_back+1)%m_max_size;
        m_array[m_back]=item;
        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();
        return 0;
    }

    //pop时候，如果队列没有元素，将会等待条件变量
    bool pop(T &item){
        m_mutex.lock();

        // while(empty()){//可以读   -------->不能用empty()函数，因为empty()内部使用了m_mutex，而外部已经用了 
        //     m_cond.wait(m_mutex);
        // }
        while(m_size<=0){
            if(!m_cond.wait(m_mutex.get())){//wait成功返回0 失败返回-1和errno
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //超时处理
    bool pop(T &item,int ms_timeout){
        struct timespec t = {0,0};
        struct timeval now = {0,0};
        gettimeofday(&now,NULL);
        m_mutex.lock();

        // 如果队列为空,等待指定的超时时间
        if(m_size<0){
            // 计算超时的绝对时间
            t.tv_sec = now.tv_sec + ms_timeout/1000; //计算超过时间的秒数部分
            t.tv_nsec = (ms_timeout%1000)*1000; //计算毫秒

            // 等待条件变量,如果超时返回false
            if(!m_cond.timewait(m_mutex.get(),t)){
                m_mutex.unlock();
                return false;
            }
        }

        // 再次检查队列是否为空(可能被其他线程消费) 因为前面解锁了
        if(m_size<=0){
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front+1)%m_max_size;
        item=m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }
    
private:
    locker m_mutex;//互斥锁对象
    cond m_cond;//条件变量对象

    T *m_array;//存放元素
    int m_size;//元素个数
    int m_max_size;//阻塞队列最大容量
    int m_front;//对头
    int m_back;//队尾
};
#endif