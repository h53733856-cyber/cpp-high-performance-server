#pragma once

#include "TaskQueue.h"

#include <cstddef>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(TaskQueue::Task task);
    void shutdown();

private:
    void worker_loop();

private:
    TaskQueue task_queue_;
    std::vector<std::thread> workers_;
};