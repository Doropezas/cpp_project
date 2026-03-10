#include "backtest/ResultAggregator.hpp"

#include <numeric>

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
    summary.num_days      = static_cast<int>(
        static_cast<double>(summary.num_days) / n);

    return summary;
}
