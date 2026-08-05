#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace dfu {

// Bounded blocking queue with a cancel path that wakes every waiter at once.
//
// cancel() is the part that has to be right: when the user hits Stop, a thread
// blocked in push() on a full queue must return immediately, not after the
// current 25 MB frame is consumed and not after a timeout.
template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer(std::size_t capacity)
        : m_capacity(capacity == 0 ? 1 : capacity)
    {
    }

    // Blocks while full. Returns false if cancelled or closed.
    bool push(T item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notFull.wait(lock, [this] { return m_cancelled || m_closed || m_items.size() < m_capacity; });

        if (m_cancelled || m_closed) {
            return false;
        }

        m_items.push_back(std::move(item));
        lock.unlock();
        m_notEmpty.notify_one();
        return true;
    }

    // Blocks while empty. Returns false once cancelled, or once closed and drained.
    bool pop(T& out)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmpty.wait(lock, [this] { return m_cancelled || m_closed || !m_items.empty(); });

        if (m_cancelled) {
            return false;
        }
        if (m_items.empty()) {
            return false; // closed and drained
        }

        out = std::move(m_items.front());
        m_items.pop_front();
        lock.unlock();
        m_notFull.notify_one();
        return true;
    }

    // Wakes every waiter immediately and discards pending items. Idempotent.
    void cancel()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_cancelled) {
                return;
            }
            m_cancelled = true;
            m_items.clear();
        }
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

    // Producer is done. Consumers drain what is left, then get false.
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed) {
                return;
            }
            m_closed = true;
        }
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

    bool cancelled() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cancelled;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    std::size_t capacity() const noexcept { return m_capacity; }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::deque<T> m_items;
    std::size_t m_capacity;
    bool m_cancelled = false;
    bool m_closed = false;
};

} // namespace dfu
