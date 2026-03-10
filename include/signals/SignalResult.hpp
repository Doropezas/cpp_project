#pragma once

#include "core/DailyBar.hpp"

#include <concepts>
#include <span>
#include <string>

// Output produced by any signal computation.
//
// value      — normalized direction in [-1.0, +1.0]
//              +1.0 = full long, -1.0 = full short, 0.0 = flat / no signal
// confidence — unsigned strength measure in [0.0, 1.0]
//              used for signal weighting in the portfolio construction step
// valid      — false when there is insufficient data to compute (e.g., warmup)

struct SignalResult {
    std::string symbol;
    double value{0.0};
    double confidence{0.0};
    bool   valid{false};
};

// C++20 concept: any type that can be called with a span of DailyBars
// and returns a SignalResult.  Satisfied by all three signal classes.
//
// This is the compile-time equivalent of an abstract base class — no
// vtable, no heap allocation, no runtime overhead.
template<typename T>
concept SignalComputable = requires(const T& s, std::span<const DailyBar> bars) {
    { s.compute(bars) } -> std::same_as<SignalResult>;
};
