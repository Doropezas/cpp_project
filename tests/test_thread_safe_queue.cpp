#include "core/ThreadSafeQueue.hpp"

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

// ── Single-threaded correctness ───────────────────────────────────────────────

static void test_push_and_try_pop() {
    ThreadSafeQueue<int> q;

    q.push(1);
    q.push(2);
    q.push(3);

    assert(q.size() == 3);

    auto v1 = q.try_pop();
    assert(v1.has_value() && *v1 == 1);

    auto v2 = q.try_pop();
    assert(v2.has_value() && *v2 == 2);

    auto v3 = q.try_pop();
    assert(v3.has_value() && *v3 == 3);

    // Queue is now empty
    auto v4 = q.try_pop();
    assert(!v4.has_value());

    std::cout << "PASS  test_push_and_try_pop\n";
}

static void test_empty_and_size() {
    ThreadSafeQueue<std::string> q;

    assert(q.empty());
    assert(q.size() == 0);

    q.push("hello");
    assert(!q.empty());
    assert(q.size() == 1);

    q.try_pop();
    assert(q.empty());

    std::cout << "PASS  test_empty_and_size\n";
}

static void test_close_drains_remaining() {
    ThreadSafeQueue<int> q;
    q.push(10);
    q.push(20);
    q.close();

    // Items pushed before close() are still available
    auto v1 = q.wait_and_pop();
    assert(v1.has_value() && *v1 == 10);

    auto v2 = q.wait_and_pop();
    assert(v2.has_value() && *v2 == 20);

    // Queue closed and empty — returns nullopt
    auto v3 = q.wait_and_pop();
    assert(!v3.has_value());

    std::cout << "PASS  test_close_drains_remaining\n";
}

static void test_push_after_close_is_noop() {
    ThreadSafeQueue<int> q;
    q.close();
    q.push(99);   // should be silently discarded

    assert(q.empty());
    auto v = q.try_pop();
    assert(!v.has_value());

    std::cout << "PASS  test_push_after_close_is_noop\n";
}

// ── Concurrent correctness ────────────────────────────────────────────────────

static void test_single_producer_single_consumer() {
    ThreadSafeQueue<int> q;
    constexpr int N = 1000;
    std::atomic<int> sum_consumed{0};

    std::thread producer([&] {
        for (int i = 1; i <= N; ++i) q.push(i);
        q.close();
    });

    std::thread consumer([&] {
        while (auto v = q.wait_and_pop()) {
            sum_consumed += *v;
        }
    });

    producer.join();
    consumer.join();

    // Sum of 1..N = N*(N+1)/2
    assert(sum_consumed == N * (N + 1) / 2);

    std::cout << "PASS  test_single_producer_single_consumer\n";
}

static void test_multiple_producers_multiple_consumers() {
    ThreadSafeQueue<int> q;
    constexpr int PRODUCERS   = 4;
    constexpr int CONSUMERS   = 4;
    constexpr int ITEMS_EACH  = 250;   // 4 * 250 = 1000 total items
    constexpr int TOTAL       = PRODUCERS * ITEMS_EACH;

    std::atomic<int> items_consumed{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < ITEMS_EACH; ++i) q.push(1);
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < CONSUMERS; ++c) {
        consumers.emplace_back([&] {
            while (auto v = q.wait_and_pop()) {
                items_consumed += *v;
            }
        });
    }

    for (auto& t : producers) t.join();
    q.close();  // signal consumers after all producers are done
    for (auto& t : consumers) t.join();

    assert(items_consumed == TOTAL);

    std::cout << "PASS  test_multiple_producers_multiple_consumers\n";
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "Running ThreadSafeQueue tests...\n\n";

    test_push_and_try_pop();
    test_empty_and_size();
    test_close_drains_remaining();
    test_push_after_close_is_noop();
    test_single_producer_single_consumer();
    test_multiple_producers_multiple_consumers();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
