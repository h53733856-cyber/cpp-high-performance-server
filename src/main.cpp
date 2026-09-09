#include<iostream>
#include<sys/socket.h>
//这是linux Socket的头文件之一，socket()就在这里声明
//socket()创建一个 Socket，并返回一个文件描述符
#include<netinet/in.h>
#include <unistd.h>


int main(){
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    //socket()创建一个 Socket，并返回一个文件描述符，listen_fd 本质上就是一个整数
    //AF_INET表示使用IPv4地址族,SOCK_STREAM表示创建一个字节流 Socket，TCP 就属于这种面向连接的字节流通信
    //最后的0是让系统根据前面的地址族和 Socket 类型自动选择对应的协议

    if (listen_fd == -1)
    {
        return 1;
    }

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

    int client_fd = accept(listen_fd, nullptr, nullptr);
    //listenfd专门监听新的客户端连接，clientfd专门和某一个已经连接的客户端通信
    //accept(listen_fd, client_address, address_length);
    if (client_fd == -1){
        return 1;
    }
    std::cout << "client_fd = " << client_fd << std::endl;
    //会输出4,fd 0→stdin,fd 1→stdout,fd 2→stderr,fd 3→listen_fd,fd 4→client_fd   
    //ccept() 完成的是“连接建立后的接入”，它只给服务器一个用于和这个客户端通信的 client_fd
    //数据是否被服务器程序读取，还需要调用 recv()

    char buffer[1024];
    //在栈上申请了一块1024字节的内存空间，用来存放从客户端接收到的数据
    int n=recv(client_fd,buffer,sizeof(buffer),0);
    //用来从一个已经连接的 Socket 中读取接收到的数据
    //recv返回的是这一次实际取到了多少字节，最后参数0表示按照默认方式接收
    //n=0表示客户端关闭了连接，n<0就是发生错误了，比如recv调用失败等等
    if (n > 0){
        std::cout.write(buffer, n);
    }
    //如果不输出，终端会直接回到命令行
    //输出的时候不能直接cout << buffer，因为对于char*，输出通常会把它当作字符串处理
    //但是字符串要以'\0'结束，这里并没有结束标志，所以使用write，从buf开始，准确输出n个字节
    if (n == 0){
        close(client_fd);
        //客户端已经关闭连接，服务器没必要再继续使用这个 client_fd 了
    }
    if (n < 0){
        close(client_fd);
    }

    send(client_fd, buffer, n, 0);
    //通过 client_fd 对应的 TCP 连接，把 buffer 中的前 n 个字节发送给客户端
    //send(通过谁发送，发送什么，发送多少字节，flags)
    return 0;
}