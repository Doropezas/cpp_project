#pragma once

#include "backtest/Backtester.hpp"
#include "backtest/PerformanceMetrics.hpp"
#include "core/ThreadPool.hpp"

#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// ResultAggregator
//
// Dispatches one Backtester::run() per symbol to a ThreadPool, collects
// std::future<RunResult>s, then merges the completed results under a
// std::scoped_lock (two mutexes: one for the per-symbol map, one for the
// aggregate metrics vector).
//
// Concurrency pattern (ARCHITECTURE.md §3.5):
//   std::scoped_lock lock{map_mutex_, metrics_mutex_};   // deadlock-safe
//
// Usage:
//   ResultAggregator agg{pool, backtester};
//   agg.add_symbol("ES", es_bars);
//   agg.add_symbol("GC", gc_bars);
//   agg.wait_all();
//   auto& results  = agg.symbol_results();     // per-symbol RunResult
//   auto  summary  = agg.portfolio_summary();  // equal-weighted combined metrics
//   auto  portfolio = agg.portfolio_pnl();     // proper cross-symbol inv-vol portfolio

class ResultAggregator
{
public:
    ResultAggregator(ThreadPool &pool, const Backtester &backtester);

    // Not copyable or movable — owns mutexes and futures.
    ResultAggregator(const ResultAggregator &) = delete;
    ResultAggregator &operator=(const ResultAggregator &) = delete;
    ResultAggregator(ResultAggregator &&) = delete;
    ResultAggregator &operator=(ResultAggregator &&) = delete;

    // Submit a backtester run for one symbol to the thread pool.
    // Returns immediately; actual computation happens on a worker thread.
    void add_symbol(const std::string &symbol,
                    std::vector<DailyBar> bars,
                    const std::string &label = "");

    // Block until all submitted runs have completed and results are merged
    // into the internal maps.
    void wait_all();

    // Per-symbol results (available after wait_all()).
    [[nodiscard]] const std::unordered_map<std::string, Backtester::RunResult> &
    symbol_results() const { return results_; }

    // Aggregate metrics vector — one entry per completed symbol.
    [[nodiscard]] const std::vector<PerformanceMetrics> &
    all_metrics() const { return metrics_; }

    // Equal-weighted portfolio summary across all symbols (simple average of metrics).
    [[nodiscard]] PerformanceMetrics portfolio_summary(
        const std::string &label = "portfolio") const;

    // Cross-symbol inverse-volatility portfolio PnL (RESEARCH.md §6).
    //
    // For each trading day t, computes:
    //   w_i(t) = (1/σ_i(t)) / Σ_j(1/σ_j(t))
    //   portfolio_pnl(t) = Σ_i direction_i(t) * w_i(t) * return_1d_i(t)
    //
    // Only symbols with a valid vol_estimate (> 0) on a given day contribute.
    // Returns PerformanceMetrics for the cross-symbol portfolio series.
    [[nodiscard]] PerformanceMetrics portfolio_pnl(
        const std::string &label = "portfolio_inv_vol") const;

private:
    ThreadPool &pool_;
    const Backtester &bt_;

    // Pending futures: one per submitted symbol.
    struct PendingJob
    {
        std::string symbol;
        std::future<Backtester::RunResult> future;
    };
    std::vector<PendingJob> pending_;

    // Protected by scoped_lock(map_mutex_, metrics_mutex_) during merge.
    mutable std::mutex map_mutex_;
    mutable std::mutex metrics_mutex_;
    std::unordered_map<std::string, Backtester::RunResult> results_;
    std::vector<PerformanceMetrics> metrics_;
};
