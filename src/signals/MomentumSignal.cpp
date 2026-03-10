#include "signals/MomentumSignal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

MomentumSignal::MomentumSignal(std::size_t lookback, std::size_t skip)
    : lookback_{lookback}, skip_{skip}
{
    if (lookback_ == 0)
        throw std::invalid_argument{"MomentumSignal: lookback must be > 0"};
    if (skip_ >= lookback_)
        throw std::invalid_argument{"MomentumSignal: skip must be < lookback"};
}

SignalResult MomentumSignal::compute(std::span<const DailyBar> bars) const {
    if (bars.empty()) return {};

    SignalResult result;
    result.symbol = bars.back().symbol;

    // Need bars indexed back to [size - 1 - lookback], i.e. lookback + 1 bars
    if (bars.size() < lookback_ + 1) return result;  // valid = false

    const std::size_t n        = bars.size();
    const double price_recent  = bars[n - 1 - skip_].close;       // P[t - skip]
    const double price_old     = bars[n - 1 - lookback_].close;   // P[t - lookback]

    if (price_old <= 0.0) return result;

    const double momentum  = price_recent / price_old - 1.0;
    result.value           = (momentum >= 0.0) ? 1.0 : -1.0;
    result.confidence      = std::min(std::abs(momentum), 1.0);  // cap at 1
    result.valid           = true;

    return result;
}
