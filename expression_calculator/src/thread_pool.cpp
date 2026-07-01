#include "expression_calculator/thread_pool.h"

#include <algorithm>
#include <stdexcept>

namespace expression_calculator {

ThreadPool::ThreadPool(size_t workerCount) {
    workerCount = std::max<size_t>(1, workerCount);
    workers_.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("thread pool is stopping");
        }
        jobs_.push(std::move(job));
    }
    cv_.notify_one();
}

size_t ThreadPool::workerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        try {   
            job();
        } catch (const std::exception& e) {
            std::cerr << "thread pool worker error: " << e.what() << std::endl;
        }
    }
}

} // namespace expression_calculator
