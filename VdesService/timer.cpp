#include "timer.h"
#include <thread>

Timer::Timer() : m_running(false) {}

Timer::~Timer() {
    stop();
}

void Timer::start(std::chrono::milliseconds interval, std::function<void()> callback, bool singleShot) {
    stop(); // 如果有旧线程，先停止
    m_interval = interval;
    m_callback = callback;
    m_singleShot = singleShot;
    m_running = true;
    m_thread = std::thread(&Timer::run, this);
}

void Timer::stop() {
    m_running = false;                     // 通知线程退出
    if (!m_thread.joinable()) return;

    // 关键修复：避免自我 join
    if (std::this_thread::get_id() == m_thread.get_id()) {
        // 当前线程就是定时器线程，不能 join 自己，改为 detach
        m_thread.detach();
    }
    else {
        m_thread.join();
    }
}

bool Timer::isRunning() const {
    return m_running;
}

void Timer::run() {
    while (m_running) {
        std::this_thread::sleep_for(m_interval);
        if (!m_running) break;
        if (m_callback) m_callback();
        if (m_singleShot) break;
    }
    // 线程退出时，m_running 已经是 false
}