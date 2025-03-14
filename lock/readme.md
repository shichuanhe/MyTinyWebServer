
/*
实现线程同步机制包装类：信号量  互斥锁  条件变量
1.符合RAII原理,确保锁的获取和释放严格遵循作用域规则
2.否则将内部同步机制暴露给外部，使得类的状态管理不可控
*/



//头文件守护：防止头文件重复包含 当多个源文件包含同一头文件时，或头文件之间存在嵌套包含关系时，#ifndef和#define的组合可确保头文件内容仅被编译一次
#ifndef LOCKER_H //LOCKER_H已定义 → 跳过头文件内容，避免重复处理
#define LOCKER_H //LOCKER_H未定义 → 执行#define LOCKER_H并包含内容

#include <exception> //异常
#include <pthread.h> //线程库
#include <semaphore.h> //信号量


//实现信号量包装类
class sem
{
private:
    sem_t m_sem;
public:
    /*
    int sem_init(sem_t *sem, int pshared, unsigned int value):初始化一个未命名的信号量
    
    1.参数：
        a.sem:指向待初始化的信号量对象的指针
        b.pshared:控制信号量的共享范围。 0：信号量在 同一进程的线程间共享   非零：信号量在进程间共享
        c.value:信号量的初始计数值
    2.返回值：
        a.成功0
        b.失败-1,并设置errno
    */
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }


    /*
    int sem_destroy(sem_t *sem): 销毁未命名信号量并释放资源

    1.参数：
        a. sem: 指向待销毁的信号量对象的指针
    2.返回值：
        a.成功返回 0
        b.失败返回 -1，并设置 errno（常见错误：EINVAL 信号量无效，EBUSY 信号量正在被等待）
    注意：销毁后不可再调用 sem_wait/sem_post 操作该信号量
    */
    ~sem()
    {
        sem_destroy(&m_sem);
    }

    /*
    int sem_wait(sem_t *sem): 以原子操作将信号量减一，若信号量为 0 则阻塞

    1.参数：
        a. sem: 指向目标信号量对象的指针
    2.返回值：
        a.成功返回 0
        b.失败返回 -1，并设置 errno
    3.行为：
        a.若信号量 > 0，立即减一并返回
        b.若信号量 = 0，阻塞直到其他线程调用 sem_post 增加信号量
    */
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    /*
    int sem_post(sem_t *sem): 以原子操作将信号量加一，并唤醒等待线程

    1.参数：
        a. sem: 指向目标信号量对象的指针
    2.返回值：
        a.成功返回 0
        b.失败返回 -1，并设置 errno
    3.行为：
        a.信号量加一后，若有线程因 sem_wait 阻塞，则唤醒其中一个
        b.若信号量原值为 0，可能触发多个等待线程的竞争
    */
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }
};


//实现互斥锁包装类
//成功返回0，失败返回errno
class locker{
private:
    pthread_mutex_t m_mutex;
public:
    lock(){
        if(pthread_mutex_init(&m_mutex,NULL)){//使用NULL表示默认互斥属性（快速互斥锁） 
            throw std::exception();
        }
    }

    ~lock(){
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock(){
        return pthread_mutex_lock(&m_mutex)==0;
    }

    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }

    /*  
    pthread_mutex_t* get()：获取原生互斥锁指针  
    1. 参数：无  
    2. 返回值：  
        a.返回内部互斥锁对象指针  
    3. 使用场景：  
        a.需要与原生pthread函数配合使用时（如pthread_cond_wait）  
        b.应避免绕过本封装直接操作互斥锁  
    */  
    pthread_mutex_t * get(){
        return &m_mutex;
    }
}

//实现条件变量
class cond{
private:
    pthread_cond_t m_cond;
public:
    cond(){
        if(pthread_cond_init(&m_cond,NULL)!=0){
            throw std::exception();
        }
    }

    ~cond(){
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex){
        int ret=0;
        ret=pthread_cond_wait(&m_cond,m_mutex);
        return ret==0;
    }

    bool timewait(pthread_mutex_t *m_mutex,struct timespec t){
        int ret=0;
        ret=pthread_cond_timedwait(&m_cond,m_mutex,&t);
        return ret==0;
    }

    bool signal(){
        return pthread_cond_signal(&m_cond)==0;
    }

    bool broadcast(){
        return pthread_cond_broadcast(&m_cond)==0;
    }
}

/*  
    int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr): 初始化条件变量  
    1. 参数：  
        a. cond: 指向待初始化的条件变量对象的指针  
        b. attr: 条件变量属性（通常为NULL表示默认属性）  
    2. 返回值：  
        a. 成功返回 0  
        b. 失败返回错误码  
    3. 注意：  
        a. 条件变量必须初始化后才能使用  
        b. 可静态初始化：pthread_cond_t cond = PTHREAD_COND_INITIALIZER  
*/  
  
/*  
    int pthread_cond_destroy(pthread_cond_t *cond): 销毁条件变量  
    1. 参数：  
        a. cond: 指向待销毁的条件变量对象的指针  
    2. 返回值：  
        a. 成功返回 0  
        b. 失败返回错误码
    3. 行为：  
        a. 释放条件变量占用的资源  
        b. 销毁后不可再进行任何等待/唤醒操作  
*/  
  
/*  
    int pthread_cond_broadcast(pthread_cond_t *cond): 唤醒所有等待线程  
    1. 参数：  
        a. cond: 指向目标条件变量对象的指针  
    2. 返回值：  
        a. 成功返回 0  
        b. 失败返回错误码 
    3. 行为：  
        a. 唤醒所有等待该条件变量的线程  
        b. 可能引发惊群效应（thundering herd problem）  
        c. 无等待线程时调用无害  
*/  
  
/*  
    int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex): 等待条件变量  该函数调用时需要传入 mutex参数(加锁的互斥锁) ,函数执行时,先把调用线程放入条件变量的请求队列,然后将互斥锁mutex解锁,当函数成功返回为0时,互斥锁会再次被锁上. 也就是说函数内部会有一次解锁和加锁操作.
    1. 参数：  
        a. cond: 指向目标条件变量对象的指针  
        b. mutex: 指向已加锁的互斥锁对象的指针（调用前必须锁定）  
    2. 返回值：  
        a. 成功返回 0  
        b. 失败返回错误码（如EINVAL无效参数，EPERM未持有互斥锁）  
    3. 行为：  
        a. 原子操作：将线程加入等待队列 → 解锁互斥锁 → 阻塞等待  
        b. 被唤醒后：重新加锁互斥锁 → 返回调用处  
    4. 注意：  
        a. 必须使用循环检查条件（防止虚假唤醒）：  
            while(condition_not_met) pthread_cond_wait(...)  
        b. mutex未提前锁定将导致未定义行为  
*/  
/*  
    int pthread_cond_signal(pthread_cond_t *cond): 唤醒一个等待线程  
    1. 参数：  
        a. cond: 指向目标条件变量对象的指针  
    2. 返回值：  
        a. 成功返回 0  
        b. 失败返回错误码（如EINVAL无效条件变量）  
    3. 行为：  
        a. 唤醒至少一个正在等待该条件变量的线程  
        b. 若无线程等待，调用无害（无副作用）  
        c. 具体唤醒线程由调度策略决定（不保证FIFO）  
    4. 注意：  
        a. 通常与互斥锁配合使用：  
            1. 调用线程应在持有互斥锁时调用该函数  
            2. 被唤醒线程需重新获取互斥锁才能继续执行  
        b. 虚假唤醒（spurious wakeup）仍需条件检查：  
            while(condition_not_met) pthread_cond_wait(...)  
        c. 对比broadcast：  
            - signal() 唤醒1个线程，资源竞争较小  
            - broadcast() 唤醒所有线程，适合状态全局变化  
*/  

