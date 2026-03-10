#include "backtest/ResultAggregator.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool approx(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) < tol;
}

static std::vector<DailyBar> make_bars(const std::string& symbol,
                                       const std::vector<double>& closes)
{
    std::vector<DailyBar> bars;
    bars.reserve(closes.size());
    double prev = 0.0;
    int day = 0;
    for (double c : closes) {
        DailyBar b;
        b.symbol    = symbol;
        b.date      = "2010-01-" + std::to_string(++day);
        b.close     = c;
        b.return_1d = (prev > 0.0) ? std::log(c / prev) : 0.0;
        prev = c;
        bars.push_back(b);
    }
    return bars;
}

static std::vector<DailyBar> make_trending(const std::string& sym,
                                           double start, double step,
                                           int count = 150)
{
    std::vector<double> closes;
    closes.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) closes.push_back(start + i * step);
    return make_bars(sym, closes);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_single_symbol_result_present() {
    ThreadPool pool{2};
    Backtester bt;
    ResultAggregator agg{pool, bt};

    agg.add_symbol("ES", make_trending("ES", 100.0, 1.0));
    agg.wait_all();

    assert(agg.symbol_results().count("ES") == 1);
    assert(agg.all_metrics().size() == 1);

    std::cout << "PASS  test_single_symbol_result_present\n";
}

static void test_two_symbols_both_present() {
    ThreadPool pool{2};
    Backtester bt;
    ResultAggregator agg{pool, bt};

    agg.add_symbol("ES", make_trending("ES", 100.0,  1.0));
    agg.add_symbol("GC", make_trending("GC", 200.0, -1.0));
    agg.wait_all();

    assert(agg.symbol_results().count("ES") == 1);
    assert(agg.symbol_results().count("GC") == 1);
    assert(agg.all_metrics().size() == 2);

    std::cout << "PASS  test_two_symbols_both_present\n";
}

static void test_parallel_results_match_sequential() {
    // Run ES and GC through the aggregator (parallel), then run each
    // through Backtester directly (sequential) and compare metrics.
    Backtester bt;
    auto es_bars = make_trending("ES", 100.0, 1.0);
    auto gc_bars = make_trending("GC", 200.0, 1.0);

    // Sequential reference
    auto es_ref = bt.run(es_bars, "ES");
    auto gc_ref = bt.run(gc_bars, "GC");

    // Parallel via aggregator
    ThreadPool pool{2};
    ResultAggregator agg{pool, bt};
    agg.add_symbol("ES", es_bars);
    agg.add_symbol("GC", gc_bars);
    agg.wait_all();

    const auto& es_par = agg.symbol_results().at("ES");
    const auto& gc_par = agg.symbol_results().at("GC");

    assert(approx(es_par.metrics.total_return, es_ref.metrics.total_return));
    assert(approx(gc_par.metrics.total_return, gc_ref.metrics.total_return));
    assert(approx(es_par.metrics.sharpe,       es_ref.metrics.sharpe));
    assert(es_par.positions.size() == es_ref.positions.size());

    std::cout << "PASS  test_parallel_results_match_sequential\n";
}

static void test_portfolio_summary_averages_metrics() {
    // Two identical symbols => portfolio summary == per-symbol metrics
    Backtester bt;
    auto bars = make_trending("X", 100.0, 1.0);

    ThreadPool pool{2};
    ResultAggregator agg{pool, bt};
    agg.add_symbol("A", bars);
    agg.add_symbol("B", bars);   // identical data, different label
    agg.wait_all();

    const auto& ma = agg.all_metrics()[0];
    const auto& mb = agg.all_metrics()[1];
    auto summary   = agg.portfolio_summary("test_portfolio");

    assert(summary.label == "test_portfolio");
    // Average of two identical values == the value itself
    assert(approx(summary.sharpe,       (ma.sharpe + mb.sharpe) / 2.0));
    assert(approx(summary.total_return, (ma.total_return + mb.total_return) / 2.0));

    std::cout << "PASS  test_portfolio_summary_averages_metrics\n";
}

static void test_empty_aggregator_summary() {
    ThreadPool pool{1};
    Backtester bt;
    ResultAggregator agg{pool, bt};
    agg.wait_all();   // nothing submitted

    auto summary = agg.portfolio_summary();
    assert(approx(summary.sharpe, 0.0));
    assert(summary.num_days == 0);

    std::cout << "PASS  test_empty_aggregator_summary\n";
}

static void test_label_forwarded_to_metrics() {
    ThreadPool pool{1};
    Backtester bt;
    ResultAggregator agg{pool, bt};

    agg.add_symbol("ES", make_trending("ES", 100.0, 1.0), "run_001");
    agg.wait_all();

    assert(agg.all_metrics()[0].label == "run_001");

    std::cout << "PASS  test_label_forwarded_to_metrics\n";
}

static void test_nine_symbols_all_complete() {
    // Simulate the full 9-symbol universe (RESEARCH.md §2) with synthetic bars.
    const std::vector<std::string> symbols =
        {"ES","NQ","YM","ZN","ZB","GC","CL","6E","6J"};

    ThreadPool pool{4};   // 4 workers, 9 jobs => tests work-stealing
    Backtester bt;
    ResultAggregator agg{pool, bt};

    double start = 100.0;
    for (const auto& sym : symbols) {
        agg.add_symbol(sym, make_trending(sym, start, 1.0));
        start += 50.0;
    }
    agg.wait_all();

    assert(agg.symbol_results().size() == 9);
    assert(agg.all_metrics().size()    == 9);

    auto summary = agg.portfolio_summary("full_universe");
    assert(summary.hit_ratio >= 0.0 && summary.hit_ratio <= 1.0);
    assert(summary.max_drawdown <= 0.0);
    assert(summary.turnover >= 0.0);

    std::cout << "PASS  test_nine_symbols_all_complete\n";
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "Running ResultAggregator tests...\n\n";

    test_single_symbol_result_present();
    test_two_symbols_both_present();
    test_parallel_results_match_sequential();
    test_portfolio_summary_averages_metrics();
    test_empty_aggregator_summary();
    test_label_forwarded_to_metrics();
    test_nine_symbols_all_complete();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
