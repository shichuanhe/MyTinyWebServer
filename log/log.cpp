#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log(){
    m_count=0;//日志行数
    m_is_async=false;//默认同步写入
}

Log::~Log()
{
    if (m_fp != NULL)  //关闭文件
    {
        fclose(m_fp);
    }
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}

//init函数实现日志创建、写入方式的判断。  异步需要设置异步需要设置阻塞队列的长度，同步不需要设置
/*
通过单例模式获取唯一的日志类，调用init方法，初始化生成日志文件，服务器启动按当前时刻创建日志，前缀为时间，后缀为自定义log文件名，并记录创建日志的时间day和行数count。
写入方式通过初始化时是否设置队列大小（表示在队列中可以放几条数据）来判断，若队列大小为0，则为同步，否则为异步。
// */
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    //设置了max_queue_size，则为异步实现
    if(max_queue_size>=1){
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;

        //flush_log_thread为回调函数,这里表示创建线程异步写日志  指在代码执行时不会阻塞程序运行的方式
        pthread_create(&tid,NULL,flush_log_thread,NULL);
    }

    m_close_log = close_log;
    
    //输出内容的长度
    m_log_buf_size=log_buf_size;
    m_buf=new char[m_log_buf_size];
    memset(m_buf,'\0',m_log_buf_size);
    
    m_split_lines=split_lines;

    time_t t=time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;


    //根据当前日期和提供的文件名构造完整的日志文件名
    const char *p = strrchr(file_name, '/');//从后往前找到第一个/的位置
    char log_full_name[256] = {0};

    //若输入的文件名没有/，则直接将时间+文件名作为日志名
    if (p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else//处理文件名包含目录路径的情况
    {
        //将/的位置向后移动一个位置，然后复制到logname中
        //p - file_name + 1是文件所在路径文件夹的长度  ->filename 指向起始位置
        //dirname相当于./hellp/world
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    
    m_fp = fopen(log_full_name, "a");//以追加模式打开日志文件
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

//eg: log.write_log(1, "User %s logged in from %s", username, ip_address);
void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;

    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //检查是否需要创建新的日志文件（每天一个新文件或达到最大行数）。
    {
        
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

       //格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        if (m_today != my_tm.tm_mday)  //日志类记录的当天时间不等于系统时间
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else//若行数超过最大行限制，在当前日志的末尾加count/max_lines为后缀创建新log
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    //将系统信息格式化后输出，具体为：格式化时间 + 格式化内容
    m_mutex.unlock();

    va_list valst;
    //将传入的format参数赋值给valst，便于格式化输出
    va_start(valst, format);//valst包含了与格式字符串format对应的参数
    
    string log_str;
    m_mutex.lock();

    //写入格式化：时间、内容
    //时间格式化，snprintf成功返回写字符的总数，其中不包括结尾的null字符
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    //内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数(不包含终止符)
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);//它会按照 format 指定的格式，将 valst 中的参数格式化为字符串。
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);//同步用fputs写日志
        m_mutex.unlock();
    }

    va_end(valst);
}