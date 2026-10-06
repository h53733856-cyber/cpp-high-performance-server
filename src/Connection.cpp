#include "Connection.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

Connection::Connection(
    int fd,
    std::uint64_t connection_id
)
    : client_fd_(fd),
      connection_id_(connection_id)
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

std::uint64_t Connection::connection_id() const
{
    return connection_id_;
}

void Connection::append_response(const std::string& response)
{
    output_buffer_.data += response;
}

// 判断输出缓冲区是否还有数据待发送
bool Connection::has_pending_data() const
{   // offset < data.size()：说明还有字节没发出去
    return output_buffer_.offset < output_buffer_.data.size();
}

//处理EPOLLIN
bool Connection::handle_read(std::vector<std::string>& requests)
{
    char buffer[1024];

    while (true) {
        ssize_t n = recv(client_fd_, buffer, sizeof(buffer), 0);

        if (n > 0) {
            // 把收到的数据交给请求解析器
            request_parser_.append(
                std::string(buffer, n)
            );

            // 一次 recv 可能包含：
            // 1. 半个请求
            // 2. 一个请求
            // 3. 多个请求
            //
            // 所以需要不断提取完整请求
            std::string request;

            while (request_parser_.next_request(request)) {
                requests.push_back(std::move(request));
            }

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

    std::cout << "[write] response sent: fd="
          << client_fd_
          << std::endl<<'\n';

    return true;
}