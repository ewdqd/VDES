#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>
#include <type_traits>   // 添加

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 0);
    ~ThreadPool();

    // 使用 invoke_result_t (C++17)
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
        using return_type = typename std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_stop) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            m_tasks.emplace([task]() { (*task)(); });
        }
        m_condition.notify_one();
        return result;
    }

    void enqueue(std::function<void()> task);

    size_t activeThreads() const { return m_activeThreads; }
    size_t pendingTasks() const {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        return m_tasks.size();
    }

private:
    void workerLoop();

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stop;
    std::atomic<size_t> m_activeThreads;
};

#endif