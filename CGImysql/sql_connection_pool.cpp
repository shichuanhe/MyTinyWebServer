#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool(){
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance(){
    static connection_pool connPool;
    return &connPool;
}

connection_pool::~connection_pool(){
    DestroyPool();
}


void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log){
    // 初始化数据库连接池的参数
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

    // 创建MaxConn个数据库连接
    for(int i = 0; i < MaxConn; i++){
        MYSQL *con = NULL;
        con = mysql_init(con);

        if(con == nullptr){
            LOG_ERROR("MySQL Error: mysql_init");
            exit(1);
        }

        // 尝试与数据库建立连接
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if(con == nullptr){
            LOG_ERROR("MySQL Error: mysql_real_connect");
            exit(1);
        }

        // 将成功建立的连接添加到连接池中
        connList.push_back(con);
        ++m_FreeConn;
    }
    
    // 初始化信号量
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}


//获取数据库连接
MYSQL* connection_pool::GetConnection(){
    if(0 == connList.size()){
        return NULL;
    }

    MYSQL *con = NULL;
    reserve.wait();

    m_lock.lock();
    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;
    m_lock.unlock();
    return con;
}


//释放数据库连接
bool connection_pool::ReleaseConnection(MYSQL *conn){
    if(nullptr == conn){
        return false;
    }

    m_lock.lock();
    connList.push_back(conn);

    ++m_FreeConn;
    --m_CurConn;
    m_lock.unlock();
    reserve.post();
    return true;
}


void connection_pool::DestroyPool(){
    m_lock.lock();
    if(connList.size() > 0){
        for(auto it = connList.begin(); it != connList.end(); ++it){
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    m_lock.unlock();
}

int connection_pool::GetFreeConn(){
    return this->m_FreeConn;
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
    *SQL = connPool->GetConnection();
    
    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}




