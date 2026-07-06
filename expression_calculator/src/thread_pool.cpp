#include "expression_calculator/thread_pool.h"

#include <algorithm>
#include <stdexcept>

namespace expression_calculator {

ThreadPool::ThreadPool(size_t workerCount) {
    workerCount = std::max<size_t>(1, workerCount);
    m_workers.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_cv.notify_all();
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            throw std::runtime_error("thread pool is stopping");
        }
        m_jobs.push(std::move(job));
    }
    m_cv.notify_one();
}

size_t ThreadPool::workerCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_workers.size();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
            if (m_stopping && m_jobs.empty()) {
                return;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop();
        }
        try {   
            job();
        } catch (const std::exception& e) {
            std::cerr << "thread pool worker error: " << e.what() << std::endl;
        }
    }
}

} // namespace expression_calculator
