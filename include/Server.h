#ifndef SERVER_H
#define SERVER_H

#include "Connection.h"

#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

//负责创建监听socket、epoll实例、接受新连接、分发IO事件、管理所有客户端连接
class Server {
public:
    Server();
    ~Server();

    // 初始化服务器
    bool init();

    // 启动服务器主循环
    void run();

    // Server持有socket资源，禁止拷贝，防止多个对象共管同一个fd造成重复close
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

private:
    //设置 fd 为非阻塞
    bool set_nonblocking(int fd);

    //接受新的客户端连接，循环accept直到没有新连接
    void accept_new_connection();

    //更新客户端在 epoll 中监听的事件
    void update_events(int client_fd);

    //关闭客户端连接
    void close_client(int client_fd);

    //处理一个 epoll 事件
    void handle_event(const epoll_event& event);

private:
    int listen_fd_;
    int epoll_fd_;

    //每一个客户端 fd 对应一个 Connection
    std::unordered_map<int, Connection> connections_;
    // 一次epoll_wait最多返回多少就绪事件
    static constexpr int MAX_EVENTS = 1024;
};

#endif