#include "TaskQueue.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    TaskQueue queue;

    std::atomic<int> counter{0};

    constexpr int TASK_COUNT = 1000;

    std::vector<std::thread> workers;//存放所有工作线程

    // 1. 创建4个工作线程（worker）
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&queue] {
            TaskQueue::Task task;
            //循环：不断从队列pop拿任务
            // pop返回true代表拿到任务，false代表队列shutdown了，退出循环，线程结束
            while (queue.pop(task)) {
                task();
            }
        });
    }

    // 2. 主线程往队列里放入1000个任务
    for (int i = 0; i < TASK_COUNT; ++i) {
        //4 个 worker 线程会争抢消费队列里的任务，谁空闲谁就 pop 取出任务执行。
        //counter是std::atomic<int>，多线程同时++counter不会出现数据竞争。如果用普通 int，会出现计数丢失
        queue.push([&counter] {
            ++counter;
        });
    }

    //不再接收新任务，剩下队列里已有的任务执行完，所有 worker 线程退出
    queue.shutdown();

    for (auto& worker : workers) {
        worker.join();
        //join()：主线程阻塞在这里，等待子线程全部跑完，才往下走
    }

    assert(counter == TASK_COUNT);

    std::cout << "TaskQueue test passed." << std::endl;

    return 0;
}