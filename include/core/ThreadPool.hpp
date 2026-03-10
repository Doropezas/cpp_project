#pragma once

#include "core/ThreadSafeQueue.hpp"

#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

// RAII wrapper around std::thread that joins automatically on destruction.
// This is what std::jthread does in C++20; we implement it manually here
// because Apple's libc++ does not yet ship std::jthread.
//
// Invariant: the underlying thread is always joined before this object
// is destroyed — no detached threads, no leaked handles.
class JThread {
public:
    template<class F>
    explicit JThread(F&& f) : thread_{std::forward<F>(f)} {}

    ~JThread() { if (thread_.joinable()) thread_.join(); }

    // Not copyable — owns a thread handle.
    JThread(const JThread&)            = delete;
    JThread& operator=(const JThread&) = delete;

    // Movable so it can live in a std::vector.
    JThread(JThread&&) noexcept            = default;
    JThread& operator=(JThread&&) noexcept = default;

private:
    std::thread thread_;
};

// A fixed-size thread pool that executes submitted tasks concurrently.
//
// Invariants:
//   - Each submitted task runs exactly once
//   - Worker threads stop cleanly when the pool is destroyed
//   - Destructor does not leak threads (jthread auto-joins)
//   - The pool is not copyable or movable
//
// Usage:
//   ThreadPool pool{4};
//   auto f = pool.submit(my_function, arg1, arg2);
//   auto result = f.get();   // blocks until the task completes

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads);

    // Close the task queue and wait for all workers to finish.
    // jthread destructors call join() automatically after the queue is closed.
    ~ThreadPool();

    // Rule of five: not copyable or movable.
    // Reason: owns threads and a queue — no sensible copy/move semantics.
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    // Submit a callable with arguments. Returns a future for the result.
    //
    // F&&, Args&&... — perfect forwarding preserves lvalue/rvalue categories.
    // std::invoke_result_t deduces the return type at compile time.
    //
    // Internally uses std::packaged_task for the future/promise link,
    // wrapped in a shared_ptr so it is copyable into std::function<void()>.
    template<class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    [[nodiscard]] std::size_t thread_count() const { return workers_.size(); }

private:
    void worker_loop();

    ThreadSafeQueue<std::function<void()>> tasks_;
    std::vector<JThread>                   workers_;
};

// ── submit() — defined in header because it is a function template ─────────

template<class F, class... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using R = std::invoke_result_t<F, Args...>;

    // Bind the callable and all arguments into a zero-argument lambda.
    // Capture by move so temporaries stay alive until the task executes.
    auto task = std::make_shared<std::packaged_task<R()>>(
        [f  = std::forward<F>(f),
         ...args = std::forward<Args>(args)]() mutable {
            return std::invoke(std::move(f), std::move(args)...);
        }
    );

    std::future<R> future = task->get_future();

    // Wrap the packaged_task in a type-erased std::function<void()>.
    // The shared_ptr keeps the task alive until the worker executes it.
    tasks_.push([task]() { (*task)(); });

    return future;
}
