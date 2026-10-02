#include<iostream>
#include<sys/socket.h>
//这是linux Socket的头文件之一，socket()就在这里声明
//socket()创建一个 Socket，并返回一个文件描述符
#include<netinet/in.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <signal.h>
#include <sys/select.h>
#include <vector>
#include <fcntl.h>
#include <sys/epoll.h>

#include <string>
#include <unordered_map>

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    //获取这个fd当前的file status flags

    if (flags == -1) {
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return false;
    }

    return true;
}

//客户端的输出缓冲区
//由于非阻塞send不保证一次把所有数据发送出去，如果send发送了一部分之后返回EAGAIN
//剩余数据必须暂时保存起来等待下一次EPOLLOUT
struct OutputBuffer{
    std::string data;
    size_t offset = 0;
};

//关闭客户端连接
void close_client(int epoll_fd, int client_fd, std::unordered_map<int,OutputBuffer>& output_buffers){
    epoll_ctl(epoll_fd,EPOLL_CTL_DEL,client_fd,nullptr);
    close(client_fd);
    output_buffers.erase(client_fd);
    //删除这个客户端对应的输出缓冲区
    std::cout << "client closed: " << client_fd << std::endl;
}

//修改客户端的epoll关注事件
//这个output_buffer是属于client_fd的，每个client连接都有独立的output_buffer
void update_events(int epoll_fd, int client_fd, const OutputBuffer& output_buffer){
    epoll_event event{};
    event.events=EPOLLIN;
    //默认开启EPOLLIN，监听客户端的读就绪事件

    //offset：已经发送成功的字节数；data.size()：缓冲区总数据长度
    //检查这个client对应的output_buffer，如果 offset < size → 还有数据留在缓冲区，需要继续发送
    if(output_buffer.offset<output_buffer.data.size()){
        event.events |= EPOLLOUT;
        //如果还有数据要发：那我们就告诉内核epoll：请监控这个client_fd的可写事件(EPOLLOUT)
    }
    event.data.fd=client_fd;//绑定事件对应的fd
    //EPOLL_CTL_MOD = modify 修改
    if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event)==-1){
        std::cerr << "epoll_ctl MOD failed: " << strerror(errno) << std::endl;
    }

}

//尝试发送客户端输出缓冲区中的数据
bool send_pending_data(int epoll_fd, int client_fd, OutputBuffer& output_buffer){
    while (output_buffer.offset < output_buffer.data.size()) {
        //data:指向还没发送的起始位置，.data()指向字符串底层字节数组的首地址
        const char* data = output_buffer.data.data() + output_buffer.offset;
        //remaining:多少字节有待发送
        size_t remaining = output_buffer.data.size() - output_buffer.offset;

        //尝试往client socket发送remaining字节
        ssize_t sent = send(client_fd, data, remaining, 0);

        //case1:成功发送了一部分
        if(sent>0){
            output_buffer.offset += sent;
            continue;
        }
        //case2:当前socket暂时不能继续发送
        //剩余数据不能丢，函数返回之后等待EPOLLOUT
        if(sent==-1 && (errno==EINTR || errno == EWOULDBLOCK)){
            update_events(epoll_fd,client_fd,output_buffer);
            return true;
        }
        //case3:其他错误
        std::cerr << "send failed: " << strerror(errno) << std::endl;
        return false;
    }

    //能走到这里，说明所有数据都发送完了,offset == data.size()
    output_buffer.data.clear();
    output_buffer.offset=0;

    update_events(epoll_fd, client_fd, output_buffer);

    return true;
}


int main(){

    signal(SIGPIPE, SIG_IGN);
    //向已经断开的TCP连接写数据可能触发SIGPIPE，默认会直接终止整个服务器进程
    //但是我们希望关闭这个clientfd然后继续accept新的客户端
    //这句话的意思是SIGPIPE->SIG_IGN->忽略这个信号


    // =========================================================
    // 1. 创建监听 Socket
    // =========================================================

    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    //socket()创建一个 Socket，并返回一个文件描述符，listen_fd 本质上就是一个整数
    //AF_INET表示使用IPv4地址族,SOCK_STREAM表示创建一个字节流 Socket，TCP 就属于这种面向连接的字节流通信
    //最后的0是让系统根据前面的地址族和 Socket 类型自动选择对应的协议

    if (listen_fd == -1)
    {
        std::cerr << "socket failed: "<< strerror(errno)<< std::endl;
        return 1;
    }

    //std::vector<int> client_fds;


    // =========================================================
    // 2. bind
    // =========================================================

    sockaddr_in server_addr{};
    //sockaddr_in是linux为ipv4地址提供的地址结构，{}是初始化，把成员初始化成0
    server_addr.sin_family = AF_INET;
    //设置成IPv4地址，和前面socket(AF_INET,SOCK_STREAM,0)对应
    server_addr.sin_port=htons(8080);
    //htons是host to network short，就是把主机字节序转换成网络字节序
    //是把程序里表示的端口号 8080 转换成 Socket 网络接口所需要的字节序
    server_addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    //htonl是把像127.0.0.1这样的32位数据转换成网络字节序
    //我们希望服务器监听127.0.0.1，这个地址叫做ipv4回环地址，INADDR_LOOPBACK，就是数据不发到互联网，而是在本机内部通信
    
    int ret=bind(listen_fd,reinterpret_cast<sockaddr*>(&server_addr),sizeof(server_addr));
    //bind()的作用就是把一个 Socket 文件描述符绑定到指定的本地 IP 地址和端口
    //sockaddr_in 是 IPv4 专用的地址结构，而 bind() 使用 sockaddr* 作为通用接口，所以需要把 sockaddr_in* 转换成 sockaddr*
    if(ret==-1){
        std::cerr << "bind failed: "<< strerror(errno)<< std::endl;
        close(listen_fd);
        return 1;
    }

    // =========================================================
    // 3. listen
    // =========================================================

    ret=listen(listen_fd,10);
    //listen的两个参数，第一个区分是哪个socket，第二个表示等待连接的队列最多放多少个
    if(ret==-1){
        std::cerr << "listen failed: "<< strerror(errno)<< std::endl;

        close(listen_fd);
        return 1;
    }

    // =========================================================
    // 4. 设置 listen_fd 为非阻塞
    // =========================================================

    if (!set_nonblocking(listen_fd)) {
        std::cerr << "failed to set listen socket non-blocking: "<< strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    // =========================================================
    // 5. 创建 epoll 实例
    // =========================================================

    int epoll_fd = epoll_create1(0);
    //epoll_fd可以理解为epoll管理器的文件描述符
    //后面可以通过epoll_ctl告诉epoll要监视哪些fd，epoll_wait等待这些fd上发生事件
    //参数0表示不设置额外标志

    if (epoll_fd == -1) {
        std::cerr << "epoll_create1 failed: "<< strerror(errno) << std::endl;

        close(listen_fd);
        //epoll创建失败要关闭创建的listenfd
        return 1;
    }

    // =========================================================
    // 6. 把 listen_fd 加入 epoll
    // =========================================================

    epoll_event event{};
    //创建一个epoll_event结构体，用于告诉epoll要监听哪个fd以及监听他的什么事件

    event.events = EPOLLIN;
    //EPOLLIN表示关心这个fd的可读事件
    //对普通 client_fd：EPOLLIN 通常表示有数据可以 recv()
    //对 listen_fd：EPOLLIN 表示已经有客户端连接到达，可以调用 accept() 接收连接

    event.data.fd = listen_fd;
    //将 listen_fd 保存到 event 中，当 epoll_wait() 返回这个事件时，可以通过 events[i].data.fd 找到到底是哪个 fd 就绪

    if (epoll_ctl(epoll_fd,EPOLL_CTL_ADD,listen_fd,&event) == -1) {
        //epoll_ctl用于修改epoll监听集合
        //epoll_fd：操作哪个epoll实例，EPOLL_STL_ADD：添加一个新的fd
        //listen_fd：要加入监听的fd，&event：监听哪些事件
        std::cerr << "epoll_ctl ADD listen_fd failed: "<< strerror(errno)<< std::endl;

        close(epoll_fd);
        close(listen_fd);
        return 1;
    }


    // =========================================================
    // 7. 准备 epoll 返回事件的数组
    // =========================================================

    constexpr int MAX_EVENTS = 1024;// 一次 epoll_wait() 最多取回 1024 个就绪事件

    std::vector<epoll_event> events(MAX_EVENTS);
    //创建长度为1024的epollevent数组，epollwait返回之后会把已经发生的事件写进数组
    //例如：events[0] -> client_fd=5 可读，events[1] -> client_fd=8 可读

    // =========================================================
    // 7.1 每个客户端对应一个输出缓冲区
    //     key:client_fd     value:OutputBuffer
    // =========================================================
    std::unordered_map<int, OutputBuffer> output_buffers;


    // =========================================================
    // 8. Server 主循环
    // =========================================================

    //v2.0加上外层while：负责Server的生命周期，反复 accept()
    //一个客户端处理完以后，继续等待下一个客户端
    while(true){
        //等待 epoll 中被监视的 fd 发生事件，readycnt表示这一次有多少个fd发生了我们关心的事件
        int ready_count = epoll_wait(
            epoll_fd,//要等待的epoll实例
            events.data(),//epoll把发生的事件写入events数组
            MAX_EVENTS,//最多返回MAX_EVENTS个事件
            -1//如果没有事件就一直等待，不会主动超时
        );

        // epoll_wait() 会一直等待，
        // 直到至少有一个已经注册的 fd 发生我们关心的事件


        if (ready_count < 0) {

            if (errno == EINTR) {
                continue;
            }//EINTR表示信号中断，重新epollwait即可

            std::cerr << "epoll_wait failed: "<< strerror(errno) << std::endl;
            break;
        }


        // =====================================================
        // 9. 遍历这一次已经就绪的事件
        // =====================================================

        for (int i = 0; i < ready_count; ++i) {
            //逐个处理
            int fd = events[i].data.fd;
            uint32_t event_flags = events[i].events;


            // =================================================
            // 情况A:fd=listenfd,说明有新的客户端连接
            // =================================================

            if (fd == listen_fd) {
                //如果发生事件的是监听socket，通常意味着已经有客户端完成TCP连接
                //调用accept把它取出来，accept会返回一个新的clientfd
                int client_fd = accept(listen_fd,nullptr,nullptr);

                if (client_fd == -1) {

                    if (errno == EINTR ||errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    //EINTR:accept被信号中断
                    //EAGAIN/EWOULDBLOCK：当前已经没有等待accept得连接

                    std::cerr << "accept failed: " << strerror(errno)<< std::endl;
                    continue;
                }


                std::cout << "client_fd = "<< client_fd<< std::endl;

                // accept() 完成的是“连接建立后的接入”
                // 它给服务器一个用于和这个客户端通信的 client_fd
                // 数据是否被服务器程序读取，还需要调用 recv()


                // ---------------------------------------------
                // 新的 client_fd 也设置成非阻塞
                // ---------------------------------------------

                if (!set_nonblocking(client_fd)) {

                    std::cerr << "failed to set client socket " "non-blocking: "<< strerror(errno)<< std::endl;
                    close(client_fd);
                    continue;
                }

                // ---------------------------------------------
                // 创建这个客户端对应的输出缓冲区
                // ---------------------------------------------

                output_buffers[client_fd] = OutputBuffer{};


                // ---------------------------------------------
                // 把 client_fd 加入 epoll
                // ---------------------------------------------

                epoll_event client_event{};
                //为这个客户端创建一个 epoll 事件描述

                client_event.events = EPOLLIN;
                //监听clientfd的可读事件，客户端发送数据之后，clientfd会变的可读，epollwait就会返回这个fd
                client_event.data.fd = client_fd;

                //把clientfd加入epoll
                if (epoll_ctl( epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {

                    std::cerr << "epoll_ctl ADD client_fd failed: "<< strerror(errno)<< std::endl;
                    output_buffers.erase(client_fd);
                    close(client_fd);
                    continue;
                }

                continue;
            }


            // =================================================
            // 情况B:客户端发生错误或者连接挂断
            // =================================================

            if (event_flags & (EPOLLERR | EPOLLHUP)) {
                //EPOLLERR：fd 发生错误，EPOLLHUP：连接发生挂断
                //&是按位与，用于判断eventflags中是否包含这些标志
                std::cerr << "client fd error/hangup: "<< fd << std::endl;

                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                //客户端发生错误的时候要记得从epoll的监听集合中删除这个fd

                close_client( epoll_fd, fd, output_buffers);

                continue;
            }


            // =================================================
            // 情况C:EPOLLIN，客户端 fd 可读
            // =================================================

            if (event_flags & EPOLLIN) {

                //❗❗❗这个循环是用来recv的，因为fd是非阻塞的，一次recv不一定把所有数据读完，所以不断recv
                //recv大于0表示读到了数据，继续读，等于0表示客户端关闭连接，停止，小于0就看errno
                //EINTR就重试，EAGAIN表示数据已经读完，停止，其他的是真正出错的
                while (true) {

                    char buffer[1024];

                    ssize_t n = recv( fd, buffer, sizeof(buffer), 0);

                    // =================================================
                    // ❗❗recv > 0
                    // 成功读取到数据
                    // =================================================

                    if (n > 0) {

                        std::cout.write(buffer, n);
                        std::cout.flush();

                        // -------------------------------------
                        // 不直接依赖这一次 send()
                        // -------------------------------------
                        OutputBuffer& output = output_buffers[fd];
                        output.data.append( buffer, n);

                        // -------------------------------------
                        // 尝试立即发送
                        // -------------------------------------
                        if (!send_pending_data(epoll_fd, fd, output)) {
                            close_client(epoll_fd, fd, output_buffers);
                            break;
                        }

                        // 这里不要 break
                        // 因为 recv() 之后还可能有更多数据，回到开头再次调用一次recv
                        continue;
                    }

                    // =================================================
                    // ❗❗recv == 0
                    // 客户端正常关闭连接
                    // =================================================

                    if (n == 0) {
                        std::cout << "client disconnected: " << fd << std::endl;
                        close_client( epoll_fd, fd, output_buffers);
                        break;
                    }

                    // =================================================
                    // ❗❗recv < 0，分不同的情况
                    // =================================================

                    //❗==================被信号中断==================
                    if (errno == EINTR) {
                        continue;
                        //回到while开头重新recv
                    }

                    //❗==================recv暂时没有更多数据==================
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        //EAGAIN 在这里是“读完了”的信号，退出recv
                        break;
                    }

                    //❗==================真正的 recv 错误==================
                    std::cerr << "recv failed: " << errno << " " << strerror(errno) << std::endl;

                    close_client(epoll_fd, fd, output_buffers);
                    break;
                }
            }
            
            // =================================================
            // 情况 D：EPOLLOUT，socket 现在可以继续发送数据
            // =================================================
            if (event_flags & EPOLLOUT) {

                auto it = output_buffers.find(fd);
                if (it == output_buffers.end()) {
                    continue;
                }

                OutputBuffer& output = it->second;

                if (!send_pending_data( epoll_fd, fd, output)) {
                    close_client( epoll_fd, fd, output_buffers);
                    continue;
                }
            }                        
        }
    }

    // =========================================================
    // 程序退出
    // =========================================================

    close(epoll_fd);

    close(listen_fd);
    
    return 0;
}