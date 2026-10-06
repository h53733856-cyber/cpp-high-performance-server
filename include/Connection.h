#ifndef CONNECTION_H
#define CONNECTION_H

#include "RequestParser.h"

#include <string>
#include <cstdint>
#include <vector>

//connection类：封装单个客户端连接，管理客户端socket，读缓冲区，写输出缓冲区
class Connection {
public:
    // 构造函数：传入客户端socket fd，explicit防止隐式类型转换
    Connection(
        int fd,
        std::uint64_t connection_id
    );
    // 析构函数：负责关闭客户端socket，释放资源
    ~Connection();

    // Connection 管理一个客户端 socket，因此不允许复制
    //Connection(const Connection&)是拷贝构造函数，=delete是显式删除这个函数，禁止拷贝
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // 获取客户端 fd，const表示不修改对象状态
    int fd() const;

    std::uint64_t connection_id() const;

    // 处理客户端的可读事件
    // 返回 true：连接仍然有效
    // 返回 false：连接已经断开或者发生错误
    bool handle_read(std::vector<std::string>& requests);

    // 处理客户端的可写事件
    // 返回 true：连接仍然有效
    // 返回 false：发送发生错误
    bool handle_write();

    // 把响应追加到输出缓冲区
    void append_response(const std::string& response);

    // 判断当前是否还有数据没有发送完
    bool has_pending_data() const;

private:
    // 客户端的输出缓冲区
    // 由于非阻塞send不保证一次把所有数据发送出去，如果send发送了一部分之后返回EAGAIN
    // 剩余数据必须暂时保存起来等待下一次EPOLLOUT
    struct OutputBuffer{
        std::string data;//待发送的字节数据
        size_t offset = 0;//offset是已经成功发送出去的字节偏移量
    };

    int client_fd_;
    std::uint64_t connection_id_;

    // 请求解析器
    RequestParser request_parser_;
    OutputBuffer output_buffer_;
};

#endif