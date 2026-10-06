#pragma once
#ifndef COMPLETION_QUEUE_H
#define COMPLETION_QUEUE_H

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>

struct Completion {
    int client_fd;
    std::uint64_t connection_id;
    std::string response;
};

class CompletionQueue {
public:
    void push(Completion completion);

    bool try_pop(Completion& completion);

private:
    std::queue<Completion> completions_;
    std::mutex mutex_;
};

#endif