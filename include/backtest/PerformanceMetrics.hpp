#pragma once

#include <string>

// Aggregate performance statistics for one backtest run.
//
// All metrics are computed over the in-sample or out-of-sample window
// depending on which slice of bars was passed to Backtester::run().
//
// sharpe        — annualized Sharpe ratio (mean(r) / std(r) * sqrt(252))
// max_drawdown  — maximum peak-to-trough drawdown (negative or zero)
// hit_ratio     — fraction of days with positive PnL  (0..1)
// turnover      — mean daily absolute position change, averaged across symbols
// total_return  — compounded return over the period (not annualized)
// num_days      — number of trading days in the window

struct PerformanceMetrics {
    std::string label;          // e.g. "in_sample" or experiment ID
    double sharpe{0.0};
    double max_drawdown{0.0};
    double hit_ratio{0.0};
    double turnover{0.0};
    double total_return{0.0};
    int    num_days{0};
};
