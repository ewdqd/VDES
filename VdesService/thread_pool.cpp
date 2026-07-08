#include "thread_pool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t numThreads) : m_stop(false), m_activeThreads(0) {
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4;
    }
    // 至少 2 个线程
    if (numThreads < 2) numThreads = 2;
    // 最多 16 个线程
    if (numThreads > 16) numThreads = 16;

    std::cout << "[ThreadPool] Creating pool with " << numThreads << " threads" << std::endl;
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    m_condition.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_stop) return;
        m_tasks.emplace(std::move(task));
    }
    m_condition.notify_one();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this]() { return m_stop || !m_tasks.empty(); });
            if (m_stop && m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        m_activeThreads++;
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[ThreadPool] Task exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[ThreadPool] Unknown task exception" << std::endl;
        }
        m_activeThreads--;
    }
}
