#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

// A thread-safe FIFO queue that supports blocking pop and graceful shutdown.
//
// Template parameter T must be movable.
//
// Invariants:
//   - Pushed values remain valid until popped (no silent drops)
//   - Waiting consumers wake up when an item is pushed OR the queue is closed
//   - After close(), wait_and_pop() drains remaining items then returns nullopt
//   - After close(), push() is a no-op (items are silently discarded)
//
// Usage:
//   Producer thread:  queue.push(value);    // never blocks
//   Consumer thread:  while (auto v = queue.wait_and_pop()) { use(*v); }
//   Shutdown:         queue.close();        // consumers exit their loops

template <typename T>
class ThreadSafeQueue
{
public:
    ThreadSafeQueue() = default;

    // Not copyable or movable — owns a mutex and condition_variable.
    ThreadSafeQueue(const ThreadSafeQueue &) = delete;
    ThreadSafeQueue &operator=(const ThreadSafeQueue &) = delete;
    ThreadSafeQueue(ThreadSafeQueue &&) = delete;
    ThreadSafeQueue &operator=(ThreadSafeQueue &&) = delete;

    // Push a value. Wakes one waiting consumer.
    // No-op if the queue has been closed.
    void push(T value)
    {
        {
            std::lock_guard lock{mutex_};
            if (closed_)
                return;
            queue_.push(std::move(value));
        }
        cv_.notify_one();
    }

    // Block until an item is available, then return it.
    // Returns std::nullopt if the queue is closed and empty.
    std::optional<T> wait_and_pop()
    {
        std::unique_lock lock{mutex_};
        cv_.wait(lock, [this]
                 { return !queue_.empty() || closed_; });

        if (queue_.empty())
            return std::nullopt; // closed with nothing left

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    // Non-blocking attempt. Returns std::nullopt if the queue is empty.
    std::optional<T> try_pop()
    {
        std::lock_guard lock{mutex_};
        if (queue_.empty())
            return std::nullopt;

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    // Signal all waiting consumers to wake up and stop.
    // Items already in the queue are still drained before consumers exit.
    void close()
    {
        {
            std::lock_guard lock{mutex_};
            closed_ = true;
        }
        cv_.notify_all();
    }

    // Snapshot of the current queue size. May be stale by the time it returns.
    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard lock{mutex_};
        return queue_.size();
    }

    [[nodiscard]] bool empty() const
    {
        std::lock_guard lock{mutex_};
        return queue_.empty();
    }

    [[nodiscard]] bool is_closed() const
    {
        std::lock_guard lock{mutex_};
        return closed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
    bool closed_{false};
};
