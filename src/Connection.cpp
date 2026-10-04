#include "Connection.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

Connection::Connection(int fd)
    : client_fd_(fd)
{
}

// 析构函数：销毁连接时关闭socket，防止文件描述符泄漏
Connection::~Connection()
{
    if (client_fd_ != -1) {
        close(client_fd_);
    }
}

int Connection::fd() const
{
    return client_fd_;
}

// 判断输出缓冲区是否还有数据待发送
bool Connection::has_pending_data() const
{   // offset < data.size()：说明还有字节没发出去
    return output_buffer_.offset < output_buffer_.data.size();
}

//处理EPOLLIN
bool Connection::handle_read()
{
    char buffer[1024];

    while (true) {
        ssize_t n = recv(client_fd_, buffer, sizeof(buffer), 0);

        if (n > 0) {
            // 打印客户端发送过来的数据
            std::cout.write(buffer, n);
            std::cout.flush();

            // 把收到的数据放入输出缓冲区
            // 后面统一通过非阻塞send发送给客户端
            output_buffer_.data.append(buffer, n);

            continue;
        }

        if (n == 0) {
            // 客户端主动关闭连接
            std::cout << "client disconnected: " << client_fd_ << std::endl;
            return false;
        }

        // recv 被信号中断
        if (errno == EINTR) {
            continue;
        }

        // 非阻塞 socket 当前已经没有更多数据可以读取
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        // 其他错误
        std::cerr << "recv failed for client " << client_fd_
                  << ": " << std::strerror(errno) << std::endl;

        return false;
    }

    return true;
}

//处理EPOLLOUT
bool Connection::handle_write()
{
    while (output_buffer_.offset < output_buffer_.data.size()) {
        //找到待发送数据起始地址：缓冲区起始 + 已经发送的偏移量
        const char* data =
            output_buffer_.data.data() + output_buffer_.offset;
        //本次还剩下多少字节未发送
        size_t remaining =
            output_buffer_.data.size() - output_buffer_.offset;

        ssize_t sent = send(client_fd_, data, remaining, 0);

        if (sent > 0) {
            // send可能只发送了一部分数据
            // 所以offset必须向前移动已经发送的字节数
            output_buffer_.offset += static_cast<size_t>(sent);
            continue;
        }

        if (sent == -1) {
            // send被信号中断
            if (errno == EINTR) {
                continue;
            }

            // 非阻塞send暂时无法继续发送
            // 剩余数据保存在output_buffer_中
            // 等待下一次EPOLLOUT
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }

            // 其他错误
            std::cerr << "send failed for client " << client_fd_
                      << ": " << std::strerror(errno) << std::endl;

            return false;
        }
    }

    // 所有数据都已经发送完成
    output_buffer_.data.clear();
    output_buffer_.offset = 0;

    return true;
}