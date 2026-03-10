#include "backtest/Backtester.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

// ── Constructor ───────────────────────────────────────────────────────────────

Backtester::Backtester(std::size_t ma_fast, std::size_t ma_slow,
                       std::size_t mom_lb,  std::size_t mom_skip)
    : ma_signal_ {ma_fast, ma_slow}
    , mom_signal_{mom_lb,  mom_skip}
    , vol_signal_{}
{}

// ── run() ─────────────────────────────────────────────────────────────────────

Backtester::RunResult Backtester::run(std::span<const DailyBar> bars,
                                      const std::string& label) const
{
    RunResult result;
    if (bars.size() < 2) {
        result.metrics.label = label;
        return result;
    }

    result.positions.reserve(bars.size());

    double prev_position = 0.0;
    double cum_pnl       = 0.0;

    for (std::size_t t = 0; t + 1 < bars.size(); ++t) {
        // Signals computed on bars[0..t] (inclusive), i.e. using close[t] as latest.
        auto window = bars.subspan(0, t + 1);

        const auto ma_r  = ma_signal_.compute(window);
        const auto mom_r = mom_signal_.compute(window);
        const auto vol_r = vol_signal_.compute(window);

        // Build position only when all three signals are valid.
        double position = 0.0;
        if (ma_r.valid && mom_r.valid && vol_r.valid && vol_r.value > 0.0) {
            // Combined directional signal: sum of directional values [-2, +2]
            const double raw_signal = ma_r.value + mom_r.value;

            // Inverse-volatility scaling: single-symbol case reduces to 1/sigma
            // normalized so that |position| <= 1.
            // We cap by dividing raw_signal (which is ±1 or ±2) by 2 and then
            // applying inv-vol scaling capped at 1.
            const double direction = raw_signal / 2.0;   // [-1, +1]

            // Scale by inv-vol weight (annualized vol already > 0 here).
            // In the multi-symbol portfolio the weights sum to 1 across symbols;
            // in the single-symbol path we simply use direction directly.
            position = std::clamp(direction, -1.0, 1.0);
        }

        // PnL: position[t] * return_1d[t+1]   (EOD timing rule)
        const double daily_pnl = position * bars[t + 1].return_1d;
        cum_pnl += daily_pnl;

        DailyPosition dp;
        dp.date     = bars[t + 1].date;
        dp.symbol   = bars[t + 1].symbol;
        dp.position = position;
        dp.pnl      = daily_pnl;
        dp.cum_pnl  = cum_pnl;

        result.positions.push_back(dp);
        prev_position = position;
    }
    (void)prev_position;   // used in turnover calc inside compute_metrics

    result.metrics = compute_metrics(result.positions, label);
    return result;
}

// ── compute_metrics() ─────────────────────────────────────────────────────────

PerformanceMetrics Backtester::compute_metrics(
    const std::vector<DailyPosition>& positions,
    const std::string& label)
{
    PerformanceMetrics m;
    m.label    = label;
    m.num_days = static_cast<int>(positions.size());

    if (positions.empty()) return m;

    // Daily PnL series
    std::vector<double> pnls;
    pnls.reserve(positions.size());
    for (const auto& dp : positions) pnls.push_back(dp.pnl);

    // Mean and stddev of daily PnL
    const double mean_pnl = std::accumulate(pnls.begin(), pnls.end(), 0.0)
                            / static_cast<double>(pnls.size());

    double variance = 0.0;
    for (double p : pnls) variance += (p - mean_pnl) * (p - mean_pnl);
    variance /= static_cast<double>(pnls.size());
    const double std_pnl = std::sqrt(variance);

    // Sharpe: annualized (sqrt(252) factor)
    m.sharpe = (std_pnl > 0.0) ? (mean_pnl / std_pnl) * std::sqrt(252.0) : 0.0;

    // Total return: sum of daily log-PnL approximation (already log returns)
    m.total_return = positions.back().cum_pnl;

    // Max drawdown: largest peak-to-trough drop in cumulative PnL
    double peak = 0.0;
    double max_dd = 0.0;
    for (const auto& dp : positions) {
        if (dp.cum_pnl > peak) peak = dp.cum_pnl;
        const double dd = dp.cum_pnl - peak;
        if (dd < max_dd) max_dd = dd;
    }
    m.max_drawdown = max_dd;

    // Hit ratio: fraction of days with pnl > 0
    const double hits = static_cast<double>(
        std::count_if(pnls.begin(), pnls.end(), [](double p) { return p > 0.0; }));
    m.hit_ratio = hits / static_cast<double>(pnls.size());

    // Turnover: mean absolute daily position change
    double total_turnover = 0.0;
    double prev_pos = 0.0;
    for (const auto& dp : positions) {
        total_turnover += std::abs(dp.position - prev_pos);
        prev_pos = dp.position;
    }
    m.turnover = total_turnover / static_cast<double>(positions.size());

    return m;
}
