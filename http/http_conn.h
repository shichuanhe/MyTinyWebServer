#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    // 常量定义
    static const int FILENAME_LEN = 200;        // 文件名最大长度
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区大小

    // HTTP请求方法枚举
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    // 主状态机状态枚举
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,  // 解析请求行
        CHECK_STATE_HEADER,           // 解析请求头
        CHECK_STATE_CONTENT           // 解析消息体
    };

    // HTTP请求的处理结果
    enum HTTP_CODE
    {
        NO_REQUEST,          // 请求不完整，需要继续读取客户数据
        GET_REQUEST,         // 获得了一个完整的客户请求
        BAD_REQUEST,         // 客户请求语法错误
        NO_RESOURCE,         // 服务器没有资源
        FORBIDDEN_REQUEST,   // 客户对资源没有足够的访问权限
        FILE_REQUEST,        // 文件请求,获取文件成功
        INTERNAL_ERROR,      // 服务器内部错误
        CLOSED_CONNECTION    // 客户端已经关闭连接
    };

    // 从状态机的三种可能状态，即行的读取状态
    enum LINE_STATUS
    {
        LINE_OK = 0,  // 读取到一个完整的行
        LINE_BAD,     // 行出错
        LINE_OPEN     // 行数据尚且不完整
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化新接受的连接  会调用私有的init()函数
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    // 关闭连接
    void close_conn(bool real_close = true);
    
    // 处理客户请求
    void process();//包含process_read() process_write()

    // 读取浏览器端发来的全部数据
    bool read_once();

    // 响应报文写入函数
    bool write();
    // 获取客户端地址
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    // 初始化数据库结果
    void initmysql_result(connection_pool *connPool);
    
    // 定时器相关
    int timer_flag;
    int improv;

private:
    // 初始化连接
    void init();

    // 从m_read_buf读取，解析HTTP请求
    HTTP_CODE process_read();

    // 写入响应报文的数据，写入m_write_buf
    bool process_write(HTTP_CODE ret);

    // 下面这一组函数被process_read调用以分析HTTP请求
    /*主状态机*/
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();//生成响应报文
    /*从状态机*/
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();

    // 这一组函数被process_write调用以填充HTTP应答
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;       // 所有socket上的事件都被注册到同一个epoll内核事件表中，所以设置成静态的
    static int m_user_count;    // 统计用户数量
    MYSQL *mysql;               // 数据库连接
    int m_state;                // 读为0, 写为1

private:
    // 该HTTP连接的socket和对方的socket地址
    int m_sockfd;
    sockaddr_in m_address;

    // 读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    // 标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置
    long m_read_idx;
    // 当前正在分析的字符在读缓冲区中的位置
    long m_checked_idx;
    // 当前正在解析的行的起始位置  
    int m_start_line;

    // 写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 写缓冲区中待发送的字节数
    int m_write_idx;

    // 主状态机当前所处的状态
    CHECK_STATE m_check_state;
    // 请求方法
    METHOD m_method;

    /*请求报文解析信息*/
    // 客户请求的目标文件的完整路径，其内容等于doc_root + m_url, doc_root是网站根目录
    char m_real_file[FILENAME_LEN];
    // 客户请求的目标文件的文件名
    char *m_url;
    // HTTP协议版本号，我们仅支持HTTP/1.1
    char *m_version;
    // 主机名
    char *m_host;
    // HTTP请求的消息体的长度
    long m_content_length;
    // HTTP请求是否要求保持连接
    bool m_linger;

    // 客户请求的目标文件被mmap到内存中的起始位置
    char *m_file_address;
    // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct stat m_file_stat;
    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    struct iovec m_iv[2];
    int m_iv_count;

    int cgi;        // 是否启用的POST
    char *m_string; // 存储请求头数据
    int bytes_to_send;  // 剩余发送字节数
    int bytes_have_send;  // 已发送字节数
    char *doc_root;  // 网站根目录

    map<string, string> m_users;  // 用户名和密码
    int m_TRIGMode;  // 触发模式
    int m_close_log;  // 是否关闭日志

    char sql_user[100];  // 数据库用户名
    char sql_passwd[100];  // 数据库密码
    char sql_name[100];  // 数据库名
};

#endif
