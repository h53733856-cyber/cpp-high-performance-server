#include "TaskQueue.h"

// 将任务压入任务队列，线程安全
void TaskQueue::push(Task task)
{
    // lock_guard：自动加锁，离开作用域自动解锁（RAII）
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Do not accept new tasks after shutdown.
        if (stopped_) {
            return;
        }
        // 将任务移动推入队列，std::move避免拷贝，提升性能
        tasks_.push(std::move(task));
    }

    // 唤醒一个正在等待的工作线程，有新任务来了
    condition_.notify_one();
}

// 阻塞等待并取出任务，task输出参数保存取出的任务
bool TaskQueue::pop(Task& task)
{   // unique_lock：可以手动unlock，支持条件变量wait（lock_guard不能用于wait）
    std::unique_lock<std::mutex> lock(mutex_);

    // condition_variable.wait(锁, 谓词)
    // 如果谓词返回false，则释放锁并阻塞当前线程；
    // 被唤醒后，重新获取锁，再次检查谓词，防止虚假唤醒
    // 虚假唤醒：操作系统可能无缘无故唤醒这个线程，没有任何人 notify
    // 等待条件：队列停止 或者 队列不为空，才退出等待
    // 自动加锁
    condition_.wait(lock, [this] {
        return stopped_ || !tasks_.empty();
    });
    //stopped_ == true（队列关闭了），!tasks_.empty() == true（队列里有任务）
    //只要上面任意一个成立，就不休眠，wait直接返回，继续往下执行
    //否则会进入wait的等待状态，阻塞


    // 退出wait之后，如果队列为空：说明是shutdown触发唤醒，没有剩余任务
    if (tasks_.empty()) {
        return false;
    }

    // 取出队首任务，move转移，减少拷贝
    task = std::move(tasks_.front());
    // 弹出队首元素（queue只pop，不返回值）
    tasks_.pop();

    return true;
}

// 关闭任务队列，停止服务
void TaskQueue::shutdown()
{
    // 加锁修改stopped_标记
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }

    // notify_all：唤醒**所有**正在wait等待的工作线程
    // 所有线程会退出wait，检测stopped_标记，安全退出循环
    condition_.notify_all();
}