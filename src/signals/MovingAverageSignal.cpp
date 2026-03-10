#include "signals/MovingAverageSignal.hpp"

#include <cassert>
#include <numeric>
#include <stdexcept>

MovingAverageSignal::MovingAverageSignal(std::size_t fast_window,
                                         std::size_t slow_window)
    : fast_{fast_window}, slow_{slow_window}
{
    if (fast_ == 0 || slow_ == 0)
        throw std::invalid_argument{"MovingAverageSignal: windows must be > 0"};
    if (fast_ >= slow_)
        throw std::invalid_argument{"MovingAverageSignal: fast must be < slow"};
}

// Compute the arithmetic mean of the last `window` closing prices.
static double trailing_mean(std::span<const DailyBar> bars, std::size_t window) {
    const std::size_t n = bars.size();
    assert(n >= window);
    double sum = 0.0;
    for (std::size_t i = n - window; i < n; ++i) sum += bars[i].close;
    return sum / static_cast<double>(window);
}

SignalResult MovingAverageSignal::compute(std::span<const DailyBar> bars) const {
    if (bars.empty()) return {};

    SignalResult result;
    result.symbol = bars.back().symbol;

    if (bars.size() < slow_) return result;   // warmup: valid = false

    const double fast_ma = trailing_mean(bars, fast_);
    const double slow_ma = trailing_mean(bars, slow_);

    result.value      = (fast_ma > slow_ma) ? 1.0 : -1.0;
    result.confidence = std::abs(fast_ma - slow_ma) / slow_ma;
    result.valid      = true;

    return result;
}
