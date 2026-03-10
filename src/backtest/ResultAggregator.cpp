#include "backtest/ResultAggregator.hpp"

#include <cmath>
#include <map>
#include <numeric>
#include <vector>

// ── Constructor ───────────────────────────────────────────────────────────────

ResultAggregator::ResultAggregator(ThreadPool& pool, const Backtester& backtester)
    : pool_{pool}, bt_{backtester}
{}

// ── add_symbol() ──────────────────────────────────────────────────────────────

void ResultAggregator::add_symbol(const std::string& symbol,
                                  std::vector<DailyBar> bars,
                                  const std::string& label)
{
    // Capture bars by move into the task lambda; symbol/label by value.
    // bt_ is captured by reference — Backtester is const and stateless.
    auto future = pool_.submit(
        [this, symbol, label, bars = std::move(bars)]() mutable {
            return bt_.run(bars, label.empty() ? symbol : label);
        }
    );

    pending_.push_back({symbol, std::move(future)});
}

// ── wait_all() ────────────────────────────────────────────────────────────────

void ResultAggregator::wait_all()
{
    for (auto& job : pending_) {
        // Block until this symbol's run is complete.
        Backtester::RunResult result = job.future.get();

        // Merge into internal maps under a scoped_lock on both mutexes.
        // std::scoped_lock acquires them in a single operation, preventing
        // deadlock regardless of acquisition order elsewhere.
        std::scoped_lock lock{map_mutex_, metrics_mutex_};
        metrics_.push_back(result.metrics);
        results_.emplace(job.symbol, std::move(result));
    }
    pending_.clear();
}

// ── portfolio_summary() ───────────────────────────────────────────────────────

PerformanceMetrics ResultAggregator::portfolio_summary(const std::string& label) const
{
    std::scoped_lock lock{map_mutex_, metrics_mutex_};

    PerformanceMetrics summary;
    summary.label = label;

    if (metrics_.empty()) return summary;

    const double n = static_cast<double>(metrics_.size());

    // Equal-weighted average of each per-symbol metric.
    for (const auto& m : metrics_) {
        summary.sharpe       += m.sharpe;
        summary.max_drawdown += m.max_drawdown;
        summary.hit_ratio    += m.hit_ratio;
        summary.turnover     += m.turnover;
        summary.total_return += m.total_return;
        summary.num_days     += m.num_days;
    }

    summary.sharpe       /= n;
    summary.max_drawdown /= n;
    summary.hit_ratio    /= n;
    summary.turnover     /= n;
    summary.total_return /= n;
    summary.num_days      = static_cast<int>(static_cast<double>(summary.num_days) / n);

    return summary;
}

// ── portfolio_pnl() ───────────────────────────────────────────────────────────
//
// Cross-symbol inverse-volatility portfolio (RESEARCH.md §6).
//
// For each trading day t shared by at least one symbol:
//   - Collect symbols with a valid vol estimate on day t (vol_estimate > 0)
//   - sum_inv_vol = Σ_i (1/σ_i)
//   - w_i = (1/σ_i) / sum_inv_vol
//   - direction_i = raw_signal_i / 2.0   (normalise [-2,+2] → [-1,+1])
//   - portfolio_position_i = direction_i * w_i
//   - portfolio_pnl_t = Σ_i portfolio_position_i * return_1d_i
//
// All dates are processed in chronological order (std::map).
// Returns PerformanceMetrics for the cross-symbol portfolio PnL series.

PerformanceMetrics ResultAggregator::portfolio_pnl(const std::string& label) const
{
    std::scoped_lock lock{map_mutex_, metrics_mutex_};

    // Collect all DailyPosition records across all symbols, indexed by date.
    // Use std::map for chronological order (YYYY-MM-DD sorts lexicographically).
    std::map<std::string, std::vector<const DailyPosition*>> by_date;

    for (const auto& [sym, run] : results_) {
        for (const auto& dp : run.positions) {
            by_date[dp.date].push_back(&dp);
        }
    }

    // Build the portfolio PnL series day by day.
    std::vector<double> port_pnls;
    port_pnls.reserve(by_date.size());

    // We need a running cumulative PnL to compute max drawdown later.
    // Build a fake position vector reusing DailyPosition for compute_metrics.
    std::vector<DailyPosition> port_positions;
    port_positions.reserve(by_date.size());

    double cum_pnl = 0.0;

    for (const auto& [date, positions] : by_date) {
        // Only use positions where vol is valid.
        double sum_inv_vol = 0.0;
        for (const auto* dp : positions) {
            if (dp->vol_estimate > 0.0)
                sum_inv_vol += 1.0 / dp->vol_estimate;
        }

        double day_pnl = 0.0;
        double day_pos = 0.0;  // aggregate position magnitude for turnover

        if (sum_inv_vol > 0.0) {
            for (const auto* dp : positions) {
                if (dp->vol_estimate <= 0.0) continue;

                const double w         = (1.0 / dp->vol_estimate) / sum_inv_vol;
                const double direction = dp->raw_signal / 2.0;  // [-1, +1]
                const double pos_i     = direction * w;

                day_pnl += pos_i * dp->return_1d;
                day_pos  += std::abs(pos_i);
            }
        }

        cum_pnl += day_pnl;

        DailyPosition dp;
        dp.date     = date;
        dp.pnl      = day_pnl;
        dp.cum_pnl  = cum_pnl;
        dp.position = day_pos;   // aggregate |position| used for turnover calc
        port_positions.push_back(dp);
    }

    return Backtester::compute_metrics(port_positions, label);
}
