#pragma once

#include <condition_variable>  //条件变量：std::condition_variable
#include <functional>          // std::function，可包装可调用对象（函数、lambda）
#include <mutex>               // 互斥锁 std::mutex、lock_guard、unique_lock
#include <queue>               // 容器适配器 std::queue，存放任务

//任务队列：多线程安全的阻塞任务队列，用于线程池
class TaskQueue {
public:
    // Task类型别名：任务是一个无参数、无返回值的可调用对象
    using Task = std::function<void()>;

    // 向队列推入一个任务
    void push(Task task);

    // 阻塞取出一个任务；有任务返回true，队列关闭且无任务返回false
    bool pop(Task& task);

    // 关闭任务队列，不再接收新任务，唤醒所有等待的工作线程
    void shutdown();

private:
    std::queue<Task> tasks_;// 存放任务的队列
    std::mutex mutex_;// 互斥锁，保护队列、stopped_，多线程访问必须加锁
    std::condition_variable condition_;// 条件变量，用来阻塞/唤醒工作线程
    bool stopped_ = false;// 标记队列是否已经关闭（shutdown）
};