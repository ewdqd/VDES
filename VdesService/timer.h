#ifndef TIMER_H
#define TIMER_H

#include <thread>
#include <chrono>
#include <functional>
#include <atomic>

class Timer {
public:
    Timer();
    ~Timer();

    void start(std::chrono::milliseconds interval, std::function<void()> callback, bool singleShot = false);
    void stop();
    bool isRunning() const;

private:
    void run();

    std::thread m_thread;
    std::atomic<bool> m_running;
    std::function<void()> m_callback;
    std::chrono::milliseconds m_interval;
    bool m_singleShot;
};

#endif