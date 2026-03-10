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

// Single-symbol daily position record produced during a backtest.
struct DailyPosition {
    std::string date;
    std::string symbol;
    double position{0.0};   // normalized weight [-1, +1]
    double pnl{0.0};        // daily PnL contribution
    double cum_pnl{0.0};    // cumulative PnL up to and including this day
};

// Backtester — Stage 5
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
// Usage:
//   Backtester bt;
//   auto [positions, metrics] = bt.run(bars, "my_label");

class Backtester {
public:
    // Signal parameters default to Constants.hpp baseline values.
    explicit Backtester(std::size_t ma_fast   = kMaFastWindow,
                        std::size_t ma_slow   = kMaSlowWindow,
                        std::size_t mom_lb    = kMomLookback,
                        std::size_t mom_skip  = kMomSkip);

    // Run the backtest over the provided bars (single symbol, chronological order).
    // Returns the per-day position log and aggregate metrics.
    struct RunResult {
        std::vector<DailyPosition> positions;
        PerformanceMetrics         metrics;
    };

    [[nodiscard]] RunResult run(std::span<const DailyBar> bars,
                                const std::string& label = "") const;

private:
    MovingAverageSignal ma_signal_;
    MomentumSignal      mom_signal_;
    VolatilitySignal    vol_signal_;

    // Compute metrics from a completed position log.
    static PerformanceMetrics compute_metrics(const std::vector<DailyPosition>& positions,
                                              const std::string& label);
};
