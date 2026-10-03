#ifndef SERVER_H
#define SERVER_H

#include "Connection.h"

#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

class Server {
public:
    Server();
    ~Server();

    // 初始化服务器
    bool init();

    // 启动服务器主循环
    void run();

    // Server 不允许复制
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

private:
    //设置 fd 为非阻塞
    bool set_nonblocking(int fd);

    //接受新的客户端连接
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

    static constexpr int MAX_EVENTS = 1024;
};

#endif