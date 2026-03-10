#include "core/ThreadPool.hpp"

ThreadPool::ThreadPool(std::size_t num_threads) {
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        // Each jthread runs worker_loop. When the thread is destroyed
        // it will call join() automatically — no manual cleanup needed.
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    // Signal all workers to stop after draining remaining tasks.
    // jthread destructors (in workers_) will join each thread.
    tasks_.close();
}

void ThreadPool::worker_loop() {
    // Block on the queue until an item arrives or the queue is closed.
    // Returns nullopt when closed and empty — exits the loop cleanly.
    while (auto task = tasks_.wait_and_pop()) {
        (*task)();
    }
}
