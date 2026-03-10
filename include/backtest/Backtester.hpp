#pragma once

#include "backtest/PerformanceMetrics.hpp"
#include "core/DailyBar.hpp"
#include "signals/MovingAverageSignal.hpp"
#include "signals/MomentumSignal.hpp"
#include "signals/VolatilitySignal.hpp"

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations for optional macro / regime support.
class MacroPanel;
class RegimeClassifier;

// Single-symbol daily position record produced during a backtest.
struct DailyPosition
{
    std::string date;
    std::string symbol;
    double position{0.0};     // final normalized weight [-1, +1] (single-symbol path)
    double pnl{0.0};          // daily PnL = position * return_1d
    double cum_pnl{0.0};      // cumulative PnL up to and including this day
    double raw_signal{0.0};   // sign(MA) + sign(mom) before vol scaling [-2, +2]
    double vol_estimate{0.0}; // per-symbol annualized σ on this day (from VolatilitySignal)
    double return_1d{0.0};    // realized log-return for this day (bars[t+1].return_1d)
};

// Backtester
//
// Runs a daily signal → position → PnL loop over a bar series.
//
// Design decisions (RESEARCH.md §3, §5, §6):
//   • EOD timing: signals on close[t] → position taken at close[t] → PnL = position * return_1d[t+1]
//   • Inverse-volatility sizing: weight_i = (1/sigma_i) / sum_j(1/sigma_j)
//     where sigma_i = VolatilitySignal on the window ending at day t
//   • Combined signal = sign(MA) + sign(momentum), then scaled by inv-vol weight
//   • Zero transaction costs in baseline (RESEARCH.md §6)
//   • Returns valid = false until all signals have sufficient warmup bars
//
// Optional regime scaling (RESEARCH.md §9):
//   If macro and regime pointers are non-null, each day's position is scaled by
//   the regime-weighted signal scalar computed from the current macro state Z_t.
//
// Usage:
//   Backtester bt;
//   auto [positions, metrics] = bt.run(bars, "my_label");
// With regime:
//   auto [positions, metrics] = bt.run(bars, "regime_label", &macro_panel, &regime_clf);

class Backtester
{
public:
    // Signal parameters default to Constants.hpp baseline values.
    explicit Backtester(std::size_t ma_fast = kMaFastWindow,
                        std::size_t ma_slow = kMaSlowWindow,
                        std::size_t mom_lb = kMomLookback,
                        std::size_t mom_skip = kMomSkip);

    // Run the backtest over the provided bars (single symbol, chronological order).
    // Returns the per-day position log and aggregate metrics.
    //
    // macro   — if non-null, macro state Z_t is looked up by date for each bar.
    // regime  — if non-null (and macro non-null), regime probabilities scale positions.
    struct RunResult
    {
        std::vector<DailyPosition> positions;
        PerformanceMetrics metrics;
    };

    [[nodiscard]] RunResult run(std::span<const DailyBar> bars,
                                const std::string &label = "",
                                const MacroPanel *macro = nullptr,
                                const RegimeClassifier *regime = nullptr) const;

    // Public: compute metrics from any position log (used for IS/OOS split reporting).
    [[nodiscard]] static PerformanceMetrics
    compute_metrics(const std::vector<DailyPosition> &positions,
                    const std::string &label);

private:
    MovingAverageSignal ma_signal_;
    MomentumSignal mom_signal_;
    VolatilitySignal vol_signal_;
};
