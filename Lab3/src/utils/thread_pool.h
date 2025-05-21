#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>

#include "../include/constants.h"

namespace raft {

/**
 * 线程池类 - 用于异步执行任务
 */
class ThreadPool {
public:
    /**
     * 构造函数
     * @param threads 线程数量
     */
    ThreadPool(size_t threads = THREAD_POOL_SIZE);
    
    /**
     * 析构函数
     */
    ~ThreadPool();
    
    /**
     * 添加任务到线程池
     * @param f 任务函数
     * @param args 任务参数
     * @return std::future 任务结果的future
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    
    /**
     * 获取当前任务队列大小
     * @return 任务队列大小
     */
    size_t getQueueSize() const;
    
    /**
     * 获取线程池大小
     * @return 线程池大小
     */
    size_t getPoolSize() const;
    
private:
    // 工作线程向量
    std::vector<std::thread> workers_;
    // 任务队列
    std::queue<std::function<void()>> tasks_;
    
    // 同步相关
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    
    // 统计信息
    std::atomic<size_t> active_tasks_;
};

// 模板方法实现
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using return_type = typename std::result_of<F(Args...)>::type;
    
    // 检查任务队列是否已满
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (tasks_.size() >= TASK_QUEUE_MAX_SIZE) {
            throw std::runtime_error("任务队列已满");
        }
    }
    
    // 创建任务包装器
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 如果线程池已停止，不再接受新任务
        if (stop_) {
            throw std::runtime_error("线程池已停止");
        }
        
        // 将任务添加到队列
        tasks_.emplace([task]() { (*task)(); });
    }
    
    // 通知一个等待中的线程
    condition_.notify_one();
    return res;
}

} // namespace raft

#endif // THREAD_POOL_H 