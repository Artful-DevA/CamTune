// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace cam {

// Single-slot "latest value wins" hand-off between threads.
//
// A producer that outruns its consumer overwrites the pending value, so stale
// frames are dropped instead of queueing up and adding latency.
template <typename T>
class Mailbox {
public:
    // Returns true if an unconsumed value was replaced (i.e. a frame was dropped).
    bool put(T value)
    {
        T old;
        bool dropped;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            dropped = m_has;
            old = std::move(m_value);
            m_value = std::move(value);
            m_has = true;
        }
        m_cv.notify_one();
        return dropped; // `old` is released outside the lock
    }

    bool tryTake(T &out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_has)
            return false;
        out = std::move(m_value);
        m_value = T();
        m_has = false;
        return true;
    }

    // Waits until a value is available, the mailbox is woken, or the timeout expires.
    template <typename Rep, typename Period>
    bool waitTake(T &out, std::chrono::duration<Rep, Period> timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, timeout, [this] { return m_has || m_woken; });
        m_woken = false;
        if (!m_has)
            return false;
        out = std::move(m_value);
        m_value = T();
        m_has = false;
        return true;
    }

    void wake()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_woken = true;
        }
        m_cv.notify_all();
    }

    void clear()
    {
        T old;
        std::lock_guard<std::mutex> lock(m_mutex);
        old = std::move(m_value);
        m_value = T();
        m_has = false;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    T m_value{};
    bool m_has = false;
    bool m_woken = false;
};

} // namespace cam
