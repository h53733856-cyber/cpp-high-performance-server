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


int main(){

    signal(SIGPIPE, SIG_IGN);
    //向已经断开的TCP连接写数据可能触发SIGPIPE，默认会直接终止整个服务器进程
    //但是我们希望关闭这个clientfd然后继续accept新的客户端
    //这句话的意思是SIGPIPE->SIG_IGN->忽略这个信号

    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    //socket()创建一个 Socket，并返回一个文件描述符，listen_fd 本质上就是一个整数
    //AF_INET表示使用IPv4地址族,SOCK_STREAM表示创建一个字节流 Socket，TCP 就属于这种面向连接的字节流通信
    //最后的0是让系统根据前面的地址族和 Socket 类型自动选择对应的协议

    if (listen_fd == -1)
    {
        return 1;
    }

    std::vector<int> client_fds;

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
        return 1;
    }

    ret=listen(listen_fd,10);
    //listen的两个参数，第一个区分是哪个socket，第二个表示等待连接的队列最多放多少个
    if(ret==-1){
        return 1;
    }

    //v2.0加上外层while：负责Server的生命周期，反复 accept()
    //一个客户端处理完以后，继续等待下一个客户端
    while(true){
        fd_set readfds;//`readfds` 用来存放**等待读事件**的 fd
        FD_ZERO(&readfds);//清空整个fd集合，把所有bit置0
        FD_SET(listen_fd, &readfds);
        //把listenfd这个监听socket加入读集合，监听这个fd的可读事件

        for (int client_fd : client_fds) {
            FD_SET(client_fd, &readfds);
        }

        int max_fd = listen_fd;

        for (int client_fd : client_fds) {
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
        }

        int ret = select(max_fd + 1, &readfds, nullptr, nullptr, nullptr);
        //int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
        //第一个参数是nfds，是最大fd值+1，select内部只会扫描0-nfds-1的bit，这里listenfd是最大fd，所以传listenfd+1
        //readfds:监听可读事件的fd集合；select 返回时会被原地修改，只剩下发生事件的 fd
        //writefds=nullptr：不监听可写事件
        //exceptfds=nullptr：不监听异常事件（带外数据）
        //timeout=nullptr：永久阻塞    ，直到有事件发生或者被信号中断

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "select failed: " << strerror(errno) << std::endl;
            break;
        }

        //int client_fd = accept(listen_fd, nullptr, nullptr);
        //listenfd专门监听新的客户端连接，clientfd专门和某一个已经连接的客户端通信
        //accept(listen_fd, client_address, address_length);
        
        if (FD_ISSET(listen_fd, &readfds)) {
            int client_fd = accept(listen_fd, nullptr, nullptr);

            if (client_fd == -1) {
                std::cerr << "accept failed: " << strerror(errno) << std::endl;
                continue;
            }

            std::cout << "client_fd = " << client_fd << std::endl;
                //会输出4,fd 0→stdin,fd 1→stdout,fd 2→stderr,fd 3→listen_fd,fd 4→client_fd   
                //ccept() 完成的是“连接建立后的接入”，它只给服务器一个用于和这个客户端通信的 client_fd
                //数据是否被服务器程序读取，还需要调用 recv()

            client_fds.push_back(client_fd);
            
        }

        for (size_t i = 0; i < client_fds.size(); ) {
            int client_fd = client_fds[i];

            if (!FD_ISSET(client_fd, &readfds)) {
                ++i;
                continue;
            }

            char buffer[1024];

            int n = recv(client_fd, buffer, sizeof(buffer), 0);

            if (n > 0) {
                std::cout.write(buffer, n);

                int total_sent = 0;

                while (total_sent < n) {
                    int sent = send(
                        client_fd,
                        buffer + total_sent,
                        n - total_sent,
                        0
                    );

                    if (sent < 0) {
                        if (errno == EINTR) {
                            continue;
                        }

                        std::cerr << "send failed: "
                                << strerror(errno) << std::endl;
                        break;
                    }

                    if (sent == 0) {
                        break;
                    }

                    total_sent += sent;
                }
            }

            else if (n == 0) {
                close(client_fd);
                client_fds.erase(client_fds.begin() + i);
                continue;
            }

            else {
                if (errno == EINTR) {
                    continue;
                }

                std::cerr << "recv failed: "
                        << strerror(errno) << std::endl;

                close(client_fd);
                client_fds.erase(client_fds.begin() + i);
                continue;
            }
            i++;
        }
        
    }
    
    return 0;
}