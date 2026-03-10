#include "signals/VolatilitySignal.hpp"

#include <cmath>

SignalResult VolatilitySignal::compute(std::span<const DailyBar> bars) const {
    if (bars.empty()) return {};

    SignalResult result;
    result.symbol = bars.back().symbol;

    // Need kVolWindow log-returns: bars[size - kVolWindow .. size - 1]
    // return_1d for bars[0] is always 0.0 (first bar of each symbol),
    // so we need at least kVolWindow + 1 bars for kVolWindow valid returns.
    if (bars.size() < kVolWindow + 1) return result;  // valid = false

    // Load the last kVolWindow returns into a RollingWindow<double, kVolWindow>
    // to demonstrate the compile-time template. The static_assert inside
    // RollingWindow fires at compile time if kVolWindow is ever set to 0.
    RollingWindow<double, kVolWindow> window;
    const std::size_t n = bars.size();
    for (std::size_t i = n - kVolWindow; i < n; ++i) {
        window.push(bars[i].return_1d);
    }

    // Annualize: sigma_annual = sigma_daily * sqrt(252)
    const double sigma_annual = window.stddev() * std::sqrt(kAnnualizationFactor);

    result.value      = sigma_annual;
    result.confidence = 1.0;
    result.valid      = true;

    return result;
}
