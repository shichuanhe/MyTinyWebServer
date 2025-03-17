#ifndef LST_TIMER
#define LST_TIMER

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
#include <vector>
#include <time.h>
#include <algorithm>
#include "../log/log.h"

class util_timer;//前向声明

struct client_data {
    sockaddr_in address;  // 客户端地址
    int sockfd;           // 客户端 socket 文件描述符
    util_timer *timer;    // 指向绑定的定时器
};

class util_timer {
public:
    util_timer() : expire(0), cb_func(nullptr), user_data(nullptr) {}

public:
    time_t expire;//超时时间  绝对时间
    void (*cb_func)(client_data *);//回调函数
    client_data *user_data;//客户数据
};

class time_heap {
public:
    time_heap();
    ~time_heap();

    void add_timer(util_timer *timer);//添加一个定时器
    void adjust_timer(util_timer *timer);//调整定时器超时时间
    void del_timer(util_timer *timer);//删除定时器
    void tick();//触发到期定时器

private:
    //维护小根堆
    void heapify_up(int index);//上浮操作  
    void heapify_down(int index);//下浮操作

    std::vector<util_timer *> heap;//小根堆存储定时器
};


//工具类（管理epoll、信号处理、定时器)
class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);//初始化 定时器时间间隔 m_TIMESLOT 主要用于 定时检测非活跃连接

    int setnonblocking(int fd);//设置文件描述符为非阻塞   防止 recv() 或 send() 在 没有数据可读写时卡死,适用于 epoll 边缘触发模式（ET模式）
    
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);//向 epoll 注册事件

    static void sig_handler(int sig);//事件处理函数  向管道写端写入信号

    void addsig(int sig, void(handler)(int), bool restart = true);//将所有信号添加到信号集中并设置信号处理函数

    void timer_handler();
    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    static int u_epollfd;
    time_heap m_timer_heap;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif