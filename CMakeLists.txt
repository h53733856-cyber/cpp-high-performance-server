cmake_minimum_required(VERSION 3.22)
    #要求 CMake 版本至少能够满足 3.22 这个版本的要求

project(cpp_high_performance_server)
    #告诉 CMake：接下来这些构建配置属于一个叫 cpp_high_performance_server 的项目

set(CMAKE_CXX_STANDARD 17)
    #set() 是 CMake 用来设置变量的命令
    #这里是告诉CMake，这个项目的源代码按照C++17的标准来编译

add_executable(tcp_server src/main.cpp)
    #创建一个可执行文件，并告诉 CMake，这个可执行文件由哪些源文件编译得到