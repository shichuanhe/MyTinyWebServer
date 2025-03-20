#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

/*-------------------------------epoll相关------------------------------*/

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);//获取当前的文件状态标志
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);//使用 F_SETFL 命令来设置新的文件状态标志
    return old_option;//函数返回原始的文件状态标志，这允许调用者在需要时恢复原始状态。
}

//添加一个fd
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode){
    //初始化 epoll_event 结构体，设置要监听的文件描述符。
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)//ET
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else //LT
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);//将event添加到epollfd
    setnonblocking(fd);
}

//从内核事件表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//重置EPOLLONESHOT事件  ->因为oneshot只允许一个sockect由单个线程处理，因此，每次处理结束后，需要重置oneshot事件
void modfd(int epollfd,int fd,int ev,int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)//ET
        event.events = ev | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
    else //LT
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}



/*---------------------------数据库连接相关--------------------------------*/

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}


/*-------------------------http初始化和关闭------------------------------*/
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close){
    if(real_close &&  (m_sockfd != -1)){
        printf("close %d\n",m_sockfd);
        removefd(m_epollfd,m_sockfd);

        m_sockfd = -1;
        m_user_count --;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}



/*-----------------------读取到buffer------------------------*/

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //LT读取数据：只进行一次 recv 调用。
    if (0 == m_TRIGMode)
    {
        // 从套接字读取数据到缓冲区
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;//更新已读取的数据量

        if (bytes_read <= 0)//如果读取失败或连接关闭（bytes_read <= 0），返回 false。
        {
            return false;
        }

        return true;
    }
    //ET读数据:在一个循环中持续读取数据  一次性读完
    else
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);


            //a. 读取出错（EAGAIN 或 EWOULDBLOCK 除外） 
            //b. 连接关闭（bytes_read == 0） 
            //c. 暂时无数据可读（EAGAIN 或 EWOULDBLOCK）
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}



/*----------------------解析报文(主从状态机)----------------------*/

http_conn::HTTP_CODE http_conn::process_read(){
    // 初始化行的状态和HTTP请求的处理结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text =0;

    // 主状态机，用于解析HTTP请求
    // 循环处理每一行，直到遇到完整的请求或者出错
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) 
           || ((line_status = parse_line()) == LINE_OK)){
        // 获取一行数据
        text = get_line();
        // 更新下一行的起始位置
        m_start_line = m_checked_idx;
        // 记录日志
        LOG_INFO("%s", text);

        // 根据当前的检查状态，调用相应的处理函数
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:
            {
                // 解析请求行
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:
            {
                // 解析请求头
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                else if(ret == GET_REQUEST) return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                // 解析请求体
                ret = parse_content(text);
                if(ret == GET_REQUEST) return do_request();
                // 请求体可能不是一行就结束，需要继续读取
                line_status = LINE_OPEN;
                break;
            }
            default:
                // 未知状态，返回内部错误
                return INTERNAL_ERROR;
        }
    }
    // 如果没有完整的请求，返回NO_REQUEST
    return NO_REQUEST;
}



//读取完整的一行   将\r\n换成\0\0
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    // 遍历缓冲区中的每个字符
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        // 检查是否遇到回车符 '\r'
        if (temp == '\r')
        {
            // 如果回车符是缓冲区中的最后一个字符，说明行不完整
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            // 如果回车符后面紧跟换行符 '\n'，说明找到了完整的行
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                // 将 '\r\n' 替换为字符串结束符 '\0'
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 如果回车符后面不是换行符，说明行格式错误
            return LINE_BAD;
        }
        // 检查是否直接遇到换行符 '\n'（可能是上一次读取的延续）
        else if (temp == '\n')
        {
            // 检查换行符前面是否是回车符
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                // 将 '\r\n' 替换为字符串结束符 '\0'
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 如果换行符前面不是回车符，说明行格式错误
            return LINE_BAD;
        }
    }
    // 如果遍历完缓冲区都没有找到行结束标志，说明行不完整
    return LINE_OPEN;
}

//解析http请求行，获得请求方法，目标url及http版本号
//在主状态机进行解析前，从状态机已经将每一行的末尾\r\n符号改为\0\0，以便于主状态机直接取出对应字符串进行处理。
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 查找请求行中的第一个空格或制表符
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    
    // 将空格或制表符替换为字符串结束符，并将指针移动到下一个字符
    *m_url++ = '\0';
    
    // 获取请求方法（如GET、POST等）
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;  // 标记为CGI请求
    }
    else
        return BAD_REQUEST;

    // 跳过URL前面的空格或制表符
    m_url += strspn(m_url, " \t");
    
    // 查找HTTP版本号
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    
    // 将URL和版本号分隔，并将指针移动到版本号
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    
    // 检查HTTP版本是否为1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    
    // 处理URL中可能包含的"http://"前缀
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    // 处理URL中可能包含的"https://"前缀
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    // 确保URL格式正确
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    
    // 如果URL只有"/"，显示默认的判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    
    // 更新解析状态为检查请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析HTTP请求头
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 如果遇到空行，说明头部解析完毕
    if (text[0] == '\0')
    {
        // 如果有消息体，则转到消息体处理状态
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 如果没有消息体，则解析完成
        return GET_REQUEST;
    }
    // 处理Connection头部
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        // 如果是keep-alive，则保持连接
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    // 处理Content-length头部
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    // 处理Host头部
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    // 其他未知头部
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    // 继续解析下一个头部
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}



/*------------------------响应报文------------------------*/

//处理HTTP请求并生成适当的响应,将请求的文件进行内存映射
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');

    //处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            
            m_lock.lock();
            if (users.find(name) == users.end())
            {
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


//向响应缓冲区添加格式化的数据
bool http_conn::add_response(const char *format,...){
    if(m_write_idx >= WRITE_BUFFER_SIZE) return false;

    va_list arg_list;
    va_start(arg_list,format);

    //使用 vsnprintf 将格式化的字符串写入缓冲区：写入位置为 m_write_buf + m_write_idx（缓冲区的当前末尾）。
    int len = vsnprintf(m_write_buf+m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    
    //写的位置不够
    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx)){
        va_end(arg_list);
        return false;
    }

    //写入成功，更新m_write_idx
    m_write_idx+=len;
    va_end(arg_list);

    LOG_INFO("request:%s",m_write_buf);
    return true;
}

bool http_conn::add_status_line(int status,const char *title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}


bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}


bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

bool http_conn::write(){
    int temp = 0;

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

//各子线程通过process函数对任务进行处理，调用process_read函数和process_write函数分别完成报文解析与报文响应两个任务。
void http_conn::process(){
    HTTP_CODE read_ret=process_read();

    if(read_ret == NO_REQUEST){//请求不完整，需要继续读取客户数据 
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);//继续监听输入（EPOLLIN）
        return;
    }

    bool write_ret=process_write(read_ret);
    if(!write_ret) close_conn();
    modfd(m_epollfd,m_sockfd,EPOLLOUT,m_TRIGMode);//修改这个套接字的 epoll 事件，监听输出就绪状态
}