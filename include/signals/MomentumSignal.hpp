#pragma once

#include "core/Constants.hpp"
#include "core/DailyBar.hpp"
#include "signals/SignalResult.hpp"

#include <cstddef>
#include <span>

// Time-series momentum signal.
//
// Signal logic (from RESEARCH.md):
//   momentum(t) = P[t - skip] / P[t - lookback] - 1
//
//   value = +1  if momentum > 0   (price continuation)
//           -1  if momentum < 0
//
//   confidence = |momentum| clamped to [0, 1]
//
// The skip window avoids contamination from short-term mean reversion.
// Returns valid = false when bars.size() < lookback + 1.

class MomentumSignal
{
public:
    MomentumSignal() = default;

    explicit MomentumSignal(std::size_t lookback, std::size_t skip);

    [[nodiscard]] SignalResult compute(std::span<const DailyBar> bars) const;

    [[nodiscard]] std::size_t lookback() const { return lookback_; }
    [[nodiscard]] std::size_t skip() const { return skip_; }

private:
    std::size_t lookback_{kMomLookback};
    std::size_t skip_{kMomSkip};
};
