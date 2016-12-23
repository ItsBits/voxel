#pragma once

#include <condition_variable>
#include <mutex>

class ThreadBarrier
{
public:
    ThreadBarrier(const int thread_count) : m_thread_count{ thread_count } {}

    void wait()
    {
        std::unique_lock<std::mutex> lock{ m_lock };

        ++m_waiting_count;

        if (m_waiting_count == m_thread_count)
        {
            m_waiting_count = 0;
            m_sign = !m_sign;
            m_condition.notify_all();
        }
        else
        {
            const auto sign_was = m_sign;
            m_condition.wait(lock, [this, sign_was] { return (sign_was != m_sign) || m_disable; });
        }
    }

    void disable()
    {
        std::unique_lock<std::mutex> lock{ m_lock };
        m_disable = true;
        m_condition.notify_all();
    }

private:
    std::condition_variable m_condition;
    std::mutex m_lock;
    int m_waiting_count{ 0 };
    const int m_thread_count;
    bool m_sign{ true };
    bool m_disable{ false };

};
