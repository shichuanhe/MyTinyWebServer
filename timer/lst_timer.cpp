#include "lst_timer.h"
#include "../http/http_conn.h"

// time_heap 实现
time_heap::time_heap() {}


//析构，释放所有的定时器
time_heap::~time_heap() {
    for (auto timer : heap) {
        delete timer;
    }
}

//添加定时器
void time_heap::add_timer(util_timer *timer) {
    if (!timer) return;
    heap.push_back(timer);//添加到队尾
    heapify_up(heap.size() - 1);//上浮 保持最小堆
}

//调整定时器超时时间
//客户端在设定时间内有数据收发,则当前时刻对该定时器重新设定时间，这里只是往后延长超时时间
//被调整的目标定时器在尾部，或定时器新的超时值仍然小于下一个定时器的超时，不用调整
//否则先将定时器从链表取出，重新插入链表
void time_heap::adjust_timer(util_timer *timer) {
    if (!timer) return;
    auto it = std::find(heap.begin(), heap.end(), timer);//二分查找遍历  找timer
    if (it != heap.end()) {
        int index = std::distance(heap.begin(), it);
        heapify_down(index);
        heapify_up(index);
    }
}

void time_heap::del_timer(util_timer *timer) {
    if (!timer) return;
    auto it = std::find(heap.begin(), heap.end(), timer);
    if (it != heap.end()) {
        int index = std::distance(heap.begin(), it);
        std::swap(heap[index], heap.back());
        heap.pop_back();
        heapify_down(index);
        delete timer;
    }
}


//遍历定时器升序链表容器，从头结点开始依次处理每个定时器，直到遇到尚未到期的定时器
//若当前时间小于定时器超时时间，跳出循环，即未找到到期的定时器
//若当前时间大于定时器超时时间，即找到了到期的定时器，执行回调函数，然后将它从链表中删除，然后继续遍历
void time_heap::tick() {
    if (heap.empty()) return;
    time_t cur = time(NULL);//获取当前时间
    
    while (!heap.empty()) {
        util_timer *timer = heap.front();
        if (timer->expire > cur) break;
        
        if (timer->cb_func) timer->cb_func(timer->user_data);//这部分代码检查定时器是否有回调函数。如果 cb_func 不为空（即已设置了回调函数），就执行回调函数。这样可以确保只有在有回调函数的情况下才会尝试执行它。
        
        del_timer(timer);
    }
}

//堆排序

void time_heap::heapify_up(int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap[parent]->expire <= heap[index]->expire) break;
        std::swap(heap[parent], heap[index]);
        index = parent;
    }
}

void time_heap::heapify_down(int index) {
    int size = heap.size();
    while (index * 2 + 1 < size) {//确保当前节点有子节点
        int left = index * 2 + 1;
        int right = left + 1;
        int smallest = left;//现假设左最小

        if (right < size && heap[right]->expire < heap[left]->expire) {//右存在且更小
            smallest = right;
        }
        if (heap[index]->expire <= heap[smallest]->expire) break;//满足堆性质，停止调整
        
        std::swap(heap[index], heap[smallest]);
        index = smallest;
    }
}

// Utils 实现

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}


int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;//返回旧的 fcntl 配置（方便恢复）
}


/*
EPOLLIN：监听 fd 可读
EPOLLET：边缘触发（ET模式），需要 while 读完数据
EPOLLRDHUP：检测 对方关闭连接
EPOLLONESHOT（可选）：防止同一个 socket 被多个线程处理
*/

//将epollfd 和 pipefd 绑定   epollfd将监听pipedfd[0]，检测是否有信号
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
     /* 开启边缘触发模式 */
    if (1 == TRIGMode) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if (one_shot) {
        event.events |= EPOLLONESHOT;//为什么要 EPOLLONESHOT？a.防止多个线程同时处理同一个fd  b.EPOLLONESHOT只触发一次，需要手动 mod 修改事件  
    }
    //event.events = EPOLLIN | EPOLLRDHUP | (TRIGMode ? EPOLLET : 0) | (one_shot ? EPOLLONESHOT : 0);
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//为信号添加处理函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发 SIGALRM 信号
void Utils::timer_handler() {
    m_timer_heap.tick();
    alarm(m_TIMESLOT);//定时
}

//向客户端显示错误信息
void Utils::show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);//向客户端发送错误信息
    close(connfd);//关闭连接
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;


//回调函数
void cb_func(client_data *user_data) {
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);//从epoll中删除该fd
    close(user_data->sockfd);//关闭连接
    http_conn::m_user_count--;//减少连接数
}
