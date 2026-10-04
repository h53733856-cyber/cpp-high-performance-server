#include "ThreadPool.h"

#include <atomic>
#include <cassert>
#include <iostream>

int main()
{
    ThreadPool pool(4);

    std::atomic<long long> result{0};

    constexpr int TASK_COUNT = 1000;

    for (int i = 1; i <= TASK_COUNT; ++i) {
        pool.submit([&result, i] {
            result.fetch_add(
                static_cast<long long>(i) * i,
                std::memory_order_relaxed
            );
        });
    }

    pool.shutdown();

    long long expected = 0;

    for (int i = 1; i <= TASK_COUNT; ++i) {
        expected += static_cast<long long>(i) * i;
    }

    assert(result == expected);

    std::cout << "ThreadPool test passed." << std::endl;

    return 0;
}