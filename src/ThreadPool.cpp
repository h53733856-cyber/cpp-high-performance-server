#include "ThreadPool.h"

#include <stdexcept>
#include <utility>

ThreadPool::ThreadPool(std::size_t thread_count)
{
    if (thread_count == 0) {
        throw std::invalid_argument(
            "ThreadPool requires at least one worker thread"
        );
    }

    workers_.reserve(thread_count);

    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

void ThreadPool::submit(TaskQueue::Task task)
{
    task_queue_.push(std::move(task));
}

void ThreadPool::shutdown()
{
    task_queue_.shutdown();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::worker_loop()
{
    TaskQueue::Task task;

    while (task_queue_.pop(task)) {
        task();
    }
}