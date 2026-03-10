#include "core/ThreadPool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <numeric>
#include <vector>

using namespace std::chrono_literals;

// ── Basic submission and result retrieval ─────────────────────────────────────

static void test_submit_returns_correct_value() {
    ThreadPool pool{2};

    auto f = pool.submit([] { return 42; });
    assert(f.get() == 42);

    std::cout << "PASS  test_submit_returns_correct_value\n";
}

static void test_submit_with_arguments() {
    ThreadPool pool{2};

    auto f = pool.submit([](int a, int b) { return a + b; }, 10, 32);
    assert(f.get() == 42);

    std::cout << "PASS  test_submit_with_arguments\n";
}

static void test_submit_string_result() {
    ThreadPool pool{2};

    auto f = pool.submit([] { return std::string{"hello"}; });
    assert(f.get() == "hello");

    std::cout << "PASS  test_submit_string_result\n";
}

// ── Parallel execution ────────────────────────────────────────────────────────

static void test_tasks_run_in_parallel() {
    // Submit N tasks that each sleep briefly. If they run sequentially
    // the total time would be N * sleep. If parallel it should be ~sleep.
    constexpr int N = 4;
    ThreadPool pool{static_cast<std::size_t>(N)};

    auto start = std::chrono::steady_clock::now();

    std::vector<std::future<int>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit([i] {
            std::this_thread::sleep_for(50ms);
            return i;
        }));
    }

    int sum = 0;
    for (auto& f : futures) sum += f.get();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Results are correct
    assert(sum == 0 + 1 + 2 + 3);

    // Ran in parallel: should be well under N * 50ms = 200ms
    assert(ms < 150);

    std::cout << "PASS  test_tasks_run_in_parallel (" << ms << "ms)\n";
}

// ── Many tasks ────────────────────────────────────────────────────────────────

static void test_many_tasks() {
    constexpr int N = 10000;
    ThreadPool pool{4};

    std::vector<std::future<int>> futures;
    futures.reserve(N);

    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit([i] { return i * i; }));
    }

    long long expected = 0;
    for (int i = 0; i < N; ++i) expected += static_cast<long long>(i) * i;

    long long actual = 0;
    for (auto& f : futures) actual += f.get();

    assert(actual == expected);

    std::cout << "PASS  test_many_tasks (" << N << " tasks)\n";
}

// ── Exception propagation ─────────────────────────────────────────────────────

static void test_exception_propagates_through_future() {
    ThreadPool pool{2};

    auto f = pool.submit([]() -> int {
        throw std::runtime_error{"task failed"};
        return 0;
    });

    bool caught{false};
    try {
        f.get();
    } catch (const std::runtime_error& e) {
        caught = (std::string{e.what()} == "task failed");
    }
    assert(caught);

    std::cout << "PASS  test_exception_propagates_through_future\n";
}

// ── Destructor ────────────────────────────────────────────────────────────────

static void test_destructor_waits_for_pending_tasks() {
    std::atomic<int> completed{0};

    {
        ThreadPool pool{2};
        for (int i = 0; i < 10; ++i) {
            pool.submit([&] {
                std::this_thread::sleep_for(5ms);
                ++completed;
            });
        }
        // Pool destructor runs here: closes queue and joins workers
    }

    // All tasks must have completed before we reach this line
    assert(completed == 10);

    std::cout << "PASS  test_destructor_waits_for_pending_tasks\n";
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "Running ThreadPool tests...\n\n";

    test_submit_returns_correct_value();
    test_submit_with_arguments();
    test_submit_string_result();
    test_tasks_run_in_parallel();
    test_many_tasks();
    test_exception_propagates_through_future();
    test_destructor_waits_for_pending_tasks();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
