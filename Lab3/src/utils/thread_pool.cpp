#include "thread_pool.h"
#include <iostream>

namespace raft {

// 构造函数
ThreadPool::ThreadPool(size_t threads)
    : stop_(false), active_tasks_(0) {
    
    // 创建工作线程
    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex_);
                    
                    // 等待条件：有任务或线程池停止
                    this->condition_.wait(lock, [this] {
                        return this->stop_ || !this->tasks_.empty();
                    });
                    
                    // 如果线程池停止且任务队列为空，退出线程
                    if (this->stop_ && this->tasks_.empty()) {
                        return;
                    }
                    
                    // 获取任务
                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }
                
                // 执行任务
                active_tasks_++;
                try {
                    task();
                } catch (const std::exception& e) {
                    std::cerr << "线程池任务执行异常: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "线程池任务执行未知异常" << std::endl;
                }
                active_tasks_--;
            }
        });
    }
    
    //std::cout << "线程池已创建，线程数: " << threads << std::endl;
}

// 析构函数
ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    
    // 通知所有等待中的线程
    condition_.notify_all();
    
    // 等待所有线程结束
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    //std::cout << "线程池已销毁" << std::endl;
}

// 获取当前任务队列大小
size_t ThreadPool::getQueueSize() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

// 获取线程池大小
size_t ThreadPool::getPoolSize() const {
    return workers_.size();
}

} // namespace raft 