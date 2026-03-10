#pragma once

#include "core/Constants.hpp"
#include "core/DailyBar.hpp"
#include "signals/SignalResult.hpp"

#include <cstddef>
#include <span>

// Moving Average Crossover signal.
//
// Signal logic (from RESEARCH.md §5.1):
//   MA_fast(t) = mean(close[t - fast + 1 .. t])
//   MA_slow(t) = mean(close[t - slow + 1 .. t])
//
//   value = +1  if MA_fast > MA_slow   (uptrend)
//           -1  if MA_fast < MA_slow   (downtrend)
//
//   confidence = |MA_fast - MA_slow| / MA_slow   (normalized spread)
//
// Returns valid = false when bars.size() < slow_window.

class MovingAverageSignal {
public:
    // Default constructor uses baseline constexpr values from Constants.hpp
    MovingAverageSignal() = default;

    explicit MovingAverageSignal(std::size_t fast_window,
                                 std::size_t slow_window);

    [[nodiscard]] SignalResult compute(std::span<const DailyBar> bars) const;

    [[nodiscard]] std::size_t fast_window() const { return fast_; }
    [[nodiscard]] std::size_t slow_window() const { return slow_; }

private:
    std::size_t fast_{kMaFastWindow};
    std::size_t slow_{kMaSlowWindow};
};
