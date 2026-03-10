#pragma once

#include "core/Constants.hpp"
#include "core/DailyBar.hpp"
#include "core/RollingWindow.hpp"
#include "signals/SignalResult.hpp"

#include <span>

// Realized volatility estimator.
//
// Not a directional signal — produces an annualized volatility estimate
// used for inverse-volatility position sizing (RESEARCH.md 6).
//
// Internally uses RollingWindow<double, kVolWindow> to demonstrate
// the compile-time window template with static_assert validation.
//
//   sigma_20(t) = sqrt(252) * std(r[t-19 .. t])
//   where r[i] = log(close[i] / close[i-1])   (already in DailyBar.return_1d)
//
//   value      = annualized volatility (positive double)
//   confidence = 1.0 when valid, 0.0 otherwise
//
// Returns valid = false when bars.size() < window + 1.

class VolatilitySignal
{
public:
    // Window is fixed to kVolWindow at compile time for RollingWindow<>.
    // A runtime parameter is accepted for configuration flexibility but
    // must equal kVolWindow when the internal RollingWindow is used.
    VolatilitySignal() = default;

    [[nodiscard]] SignalResult compute(std::span<const DailyBar> bars) const;

    [[nodiscard]] std::size_t window() const { return kVolWindow; }
};
