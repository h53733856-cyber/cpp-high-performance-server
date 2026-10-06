#include "CompletionQueue.h"

void CompletionQueue::push(Completion completion)
{
    std::lock_guard<std::mutex> lock(mutex_);

    completions_.push(std::move(completion));
}

bool CompletionQueue::try_pop(Completion& completion)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (completions_.empty()) {
        return false;
    }

    completion = std::move(completions_.front());
    completions_.pop();

    return true;
}