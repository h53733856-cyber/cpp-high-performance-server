#ifndef CONNECTION_H
#define CONNECTION_H

#include <string>

class Connection {
public:
    explicit Connection(int fd);
    ~Connection();

    // Connection 管理一个客户端 socket，因此不允许复制
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // 获取客户端 fd
    int fd() const;

    // 处理客户端的可读事件
    // 返回 true：连接仍然有效
    // 返回 false：连接已经断开或者发生错误
    bool handle_read();

    // 处理客户端的可写事件
    // 返回 true：连接仍然有效
    // 返回 false：发送发生错误
    bool handle_write();

    // 判断当前是否还有数据没有发送完
    bool has_pending_data() const;

private:
    // 客户端的输出缓冲区
    // 由于非阻塞send不保证一次把所有数据发送出去，如果send发送了一部分之后返回EAGAIN
    // 剩余数据必须暂时保存起来等待下一次EPOLLOUT
    struct OutputBuffer{
        std::string data;
        size_t offset = 0;
    };

    int client_fd_;
    OutputBuffer output_buffer_;
};

#endif