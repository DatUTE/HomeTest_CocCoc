#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace expression_calculator {

/**
 * @brief Fixed-size worker pool for CPU-bound expression evaluation jobs.
 *
 * The pool owns a stable set of worker threads and executes submitted jobs in
 * FIFO order. It is used by the TCP server to limit concurrent calculations to
 * the configured number of CPU workers.
 */
class ThreadPool {
public:
    /**
     * @brief Starts the worker pool.
     * @param workerCount Number of worker threads to create. Values below 1 are clamped to 1.
     */
    explicit ThreadPool(size_t workerCount);

    /**
     * @brief Stops the pool after queued jobs finish and joins all worker threads.
     */
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Enqueues a job for asynchronous execution by a worker thread.
     * @param job Callable object to execute.
     * @throws std::runtime_error If the pool is already stopping.
     */
    void submit(std::function<void()> job);

    /**
     * @brief Returns the number of worker threads owned by the pool.
     * @return Worker thread count.
     */
    size_t workerCount() const;

private:
    /**
     * @brief Main loop executed by each worker thread.
     */
    void workerLoop();

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::function<void()>> m_jobs;
    std::vector<std::thread> m_workers;
    bool m_stopping = false;
};

} // namespace expression_calculator
