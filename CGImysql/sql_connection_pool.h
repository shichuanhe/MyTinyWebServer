//懒汉+链表实现数据库连接池  
//私有化它的构造函数和析构函数，以防止外界创建单例类的对象；使用类的私有静态指针变量指向类的唯一实例，并用一个公有的静态方法获取该实例。
#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;
class connection_pool{
public:
    static connection_pool *GetInstance();

    /*初始化数据库连接池*/
    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 

    MYSQL *GetConnection();//获取数据库连接
    bool ReleaseConnection(MYSQL *conn);//释放连接
    int GetFreeConn();//获取连接
    void DestroyPool();//销毁所有连接
private:
    connection_pool();
    ~connection_pool();
private:
    int m_MaxConn;//最大连接数
    int m_FreeConn;//可用连接数
    int m_CurConn;//已用连接数
    list<MYSQL *> connList;//连接池
 
    locker m_lock;
    sem reserve;//信号量记录可用资源
public:
    string m_url;//主机地址
    string m_Port;//数据库端口号
    string m_User;//数据库用户名
    string m_PassWord;//数据库密码
    string m_DatabaseName;//数据库名
    int m_close_log;//是否开启日志
};


class connectionRAII{
public:
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();
private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};

#endif