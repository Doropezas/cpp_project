#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

// Fixed-size circular buffer that tracks the N most recent values and
// provides common rolling statistics (mean, population stddev).
//
// Template parameter N is the window size — a compile-time constant.
// static_assert enforces valid values at compile time with a clear message.
//
// Example:
//   RollingWindow<double, 20> vol_window;
//   vol_window.push(return_t);
//   if (vol_window.full()) double sigma = vol_window.stddev();

template<typename T, std::size_t N>
class RollingWindow {
    static_assert(N > 0,     "RollingWindow: N must be at least 1");
    static_assert(N <= 10000, "RollingWindow: N is unreasonably large");

public:
    RollingWindow() = default;

    // Add a new value, evicting the oldest when the buffer is full.
    void push(T value) {
        buf_[head_] = std::move(value);
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }

    // True once N values have been pushed.
    [[nodiscard]] bool full()  const { return count_ == N; }
    [[nodiscard]] std::size_t size() const { return count_; }

    // Most recently pushed value.
    [[nodiscard]] T latest() const {
        assert(count_ > 0 && "latest() called on empty RollingWindow");
        return buf_[(head_ == 0 ? N : head_) - 1];
    }

    // Arithmetic mean of all values currently in the window.
    [[nodiscard]] T mean() const {
        assert(count_ > 0 && "mean() called on empty RollingWindow");
        T sum{};
        for (std::size_t i = 0; i < count_; ++i) sum += buf_[i];
        return sum / static_cast<T>(count_);
    }

    // Population standard deviation of values in the window.
    // Requires at least 2 values.
    [[nodiscard]] T stddev() const {
        assert(count_ > 1 && "stddev() requires at least 2 values");
        T m = mean();
        T variance{};
        for (std::size_t i = 0; i < count_; ++i) {
            T diff = buf_[i] - m;
            variance += diff * diff;
        }
        return std::sqrt(variance / static_cast<T>(count_));
    }

    // Read-only view of the raw buffer storage.
    // Note: elements are NOT in chronological order when the buffer has wrapped.
    // Use for bulk operations (e.g., feeding to an external algorithm).
    [[nodiscard]] std::span<const T> raw_view() const {
        return std::span<const T>{buf_.data(), count_};
    }

    void clear() { head_ = 0; count_ = 0; }

private:
    std::array<T, N> buf_{};
    std::size_t      head_{0};
    std::size_t      count_{0};
};
