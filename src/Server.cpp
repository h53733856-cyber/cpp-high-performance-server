#include "Server.h"
#include "RequestHandler.h"

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <signal.h>
#include <fcntl.h>

//构造函数：初始化fd为-1，代表无效
Server::Server()
    : listen_fd_(-1),
      epoll_fd_(-1),
      thread_pool_(4)
{
}

Server::~Server()
{   //epoll_fd有效就关闭epoll实例
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
    }
    //监听fd有效就关闭监听socket
    if (listen_fd_ != -1) {
        close(listen_fd_);
    }
}

bool Server::set_nonblocking(int fd)
{
    //fcntl:获取当前 fd 的文件状态标志
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        std::cerr << "fcntl F_GETFL failed for fd "
                  << fd << ": " << std::strerror(errno)
                  << std::endl;

        return false;
    }

    //在原来的 flags 上增加 O_NONBLOCK
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "fcntl F_SETFL failed for fd "
                  << fd
                  << ": "
                  << std::strerror(errno)
                  << std::endl;

        return false;
    }

    return true;
}

bool Server::init()
{
    //忽略SIGPIPE
    //如果对端已经关闭连接，而我们仍然调用send()
    //系统可能产生SIGPIPE信号，默认情况下SIGPIPE会终止整个进程
    //这里把SIGPIPE设置为忽略
    signal(SIGPIPE, SIG_IGN);

    //1. 创建监听 Socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd_ == -1) {
        std::cerr << "socket failed: "
                  << std::strerror(errno)
                  << std::endl;

        return false;
    }

    //2. bind绑定地址端口
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&server_addr),
            sizeof(server_addr)) == -1) {

        std::cerr << "bind failed: "
                  << std::strerror(errno)
                  << std::endl;

        return false;
    }

    //3. listen开始监听，第二个参数是挂起连接队列长度
    if (listen(listen_fd_, 10) == -1) {
        std::cerr << "listen failed: "
                  << std::strerror(errno)
                  << std::endl;

        return false;
    }

    //4. 设置 listen_fd 为非阻塞
    if (!set_nonblocking(listen_fd_)) {
        return false;
    }

    //5. 创建 epoll 实例
    epoll_fd_ = epoll_create1(0);

    if (epoll_fd_ == -1) {
        std::cerr << "epoll_create1 failed: "
                  << std::strerror(errno)
                  << std::endl;

        return false;
    }

    //6. 把 listen_fd 加入 epoll
    epoll_event listen_event{};
    listen_event.events = EPOLLIN;
    listen_event.data.fd = listen_fd_;

    if (epoll_ctl(
            epoll_fd_,
            EPOLL_CTL_ADD,
            listen_fd_,
            &listen_event) == -1) {

        std::cerr << "epoll_ctl ADD listen_fd failed: "
                  << std::strerror(errno)
                  << std::endl;

        return false;
    }

    return true;
}

void Server::accept_new_connection()
{
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_addr_len
        );

        if (client_fd == -1) {
            //accept被信号中断
            if (errno == EINTR) {
                continue;
            }

            //非阻塞listen socket当前没有更多连接
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            std::cerr << "accept failed: "
                      << std::strerror(errno)
                      << std::endl;

            break;
        }

        std::cout << "new client connected: "
                  << client_fd
                  << std::endl;

        //客户端socket也必须设置为非阻塞
        if (!set_nonblocking(client_fd)) {
            close(client_fd);
            continue;
        }

        //创建这个客户端对应的Connection
        //key=client_fd，value用client_fd构造Connection
        auto [it, inserted] =
            connections_.try_emplace(client_fd, client_fd);

        if (!inserted) {
            std::cerr << "failed to create Connection for client "
                      << client_fd
                      << std::endl;

            close(client_fd);
            continue;
        }

        //把客户端socket加入epoll，初始只监听EPOLLIN可读事件
        epoll_event client_event{};
        client_event.events = EPOLLIN;
        client_event.data.fd = client_fd;

        if (epoll_ctl(
                epoll_fd_,
                EPOLL_CTL_ADD,
                client_fd,
                &client_event) == -1) {

            std::cerr << "epoll_ctl ADD client failed: "
                      << std::strerror(errno)
                      << std::endl;

            connections_.erase(it);
            continue;
        }
    }
}

//更新epoll监听事件：根据缓冲区是否有待发数据，动态选择EPOLLIN 或者 EPOLLIN|EPOLLOUT
void Server::update_events(int client_fd)
{
    auto it = connections_.find(client_fd);

    if (it == connections_.end()) {
        return;
    }

    Connection& connection = it->second;

    epoll_event event{};

    //默认监听EPOLLIN
    event.events = EPOLLIN;

    //如果输出缓冲区还有没有发送完的数据
    //说明当前还需要等待socket可写
    //因此增加EPOLLOUT
    if (connection.has_pending_data()) {
        event.events |= EPOLLOUT;
    }

    event.data.fd = client_fd;

    if (epoll_ctl(
            epoll_fd_,
            EPOLL_CTL_MOD,
            client_fd,
            &event) == -1) {

        std::cerr << "epoll_ctl MOD failed for client "
                  << client_fd
                  << ": "
                  << std::strerror(errno)
                  << std::endl;
    }
}


void Server::submit_requests(int client_fd, const std::vector<std::string>& requests)
{
    for (const std::string& request : requests) {

        thread_pool_.submit(
            [client_fd, request]() {

                RequestHandler handler;

                std::string response =
                    handler.process_request(request);

                // Step 4 暂时只验证 Worker 正确执行了业务。
                //
                // 现在还不能直接 send()。
                // Worker 不允许操作 Connection 或 epoll。
                //
                // Step 5 会通过 CompletionQueue + eventfd
                // 把 response 返回给 I/O 线程。
                std::cout << "[worker] client " << client_fd << " request: "
                    << request << " response: " << response << std::endl;
            }
        );
    }
}

void Server::close_client(int client_fd)
{
    //从epoll中删除这个客户端
    epoll_ctl(
        epoll_fd_,
        EPOLL_CTL_DEL,
        client_fd,
        nullptr
    );

    auto it = connections_.find(client_fd);

    if (it != connections_.end()) {
        //删除这个客户端对应的Connection
        //Connection析构时会关闭socket
        //同时它内部的输出缓冲区也会一起释放
        connections_.erase(it);
    }

    std::cout << "client closed: " << client_fd << std::endl;
}

void Server::handle_event(const epoll_event& event)
{
    int fd = event.data.fd;
    uint32_t event_flags = event.events;

    //监听socket发生EPOLLIN
    //说明有新的客户端连接到来
    if (fd == listen_fd_) {
        accept_new_connection();
        return;
    }

    //客户端发生错误或者挂起
    if (event_flags & (EPOLLERR | EPOLLHUP)) {
        std::cerr << "client error or hangup: "
                  << fd
                  << std::endl;

        close_client(fd);
        return;
    }

    auto it = connections_.find(fd);

    if (it == connections_.end()) {
        return;
    }

    Connection& connection = it->second;

    //客户端socket可读
    if (event_flags & EPOLLIN) {
        std::vector<std::string> requests;

        //处理客户端发送过来的数据
        if (!connection.handle_read(requests)) {
            close_client(fd);
            return;
        }

        // 把完整请求提交给 Worker
        if (!requests.empty()) {
            submit_requests(fd, requests);
        }

        //根据当前输出缓冲区状态
        //决定下一次epoll应该监听什么事件
        // Step 4 暂时没有 Worker -> I/O 的结果返回，
        // 所以这里不再立即 send
        update_events(fd);
    }

    //客户端socket可写
    if (event_flags & EPOLLOUT) {
        auto current = connections_.find(fd);

        if (current == connections_.end()) {
            return;
        }

        Connection& current_connection = current->second;

        //只有还有数据没有发送完时才需要处理EPOLLOUT
        if (current_connection.has_pending_data()) {
            if (!current_connection.handle_write()) {
                close_client(fd);
                return;
            }
        }

        //如果数据全部发送完成
        //取消EPOLLOUT，只继续监听EPOLLIN
        update_events(fd);
    }
}

void Server::run()
{
    //7. 准备 epoll 返回事件的数组
    std::vector<epoll_event> events(MAX_EVENTS);

    //8. Server 主循环
    while (true) {
        int ready_count = epoll_wait(
            epoll_fd_,
            events.data(),
            MAX_EVENTS,
            -1
        );

        if (ready_count == -1) {
            //epoll_wait被信号中断
            //不是服务器错误，重新等待即可
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "epoll_wait failed: "
                      << std::strerror(errno)
                      << std::endl;

            break;
        }

        //9. 遍历这一次已经就绪的事件
        for (int i = 0; i < ready_count; ++i) {
            handle_event(events[i]);
        }
    }
}