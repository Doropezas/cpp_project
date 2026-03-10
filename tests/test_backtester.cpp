#include "backtest/Backtester.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool approx(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) < tol;
}

// Build a bar series with given close prices; return_1d = log(c[t]/c[t-1]).
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

// Build a long enough bar series (130 bars) for all signals to warm up:
//   MA needs slow_window = 100 bars
//   Momentum needs lookback + 1 = 61 bars
//   Vol needs kVolWindow + 1 = 21 bars
// So warmup ends after bar index 99 (100 bars seen).
// First live PnL day is bar[100] → return_1d[101], giving 29 active days.
static std::vector<DailyBar> make_trending_bars(double start, double step,
                                                int count = 130)
{
    std::vector<double> closes;
    closes.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) closes.push_back(start + i * step);
    return make_bars("ES", closes);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_empty_bars_returns_zero_metrics() {
    Backtester bt;
    auto [positions, metrics] = bt.run({});

    assert(positions.empty());
    assert(metrics.num_days == 0);
    assert(approx(metrics.sharpe, 0.0));

    std::cout << "PASS  test_empty_bars_returns_zero_metrics\n";
}

static void test_insufficient_bars_no_active_days() {
    // Only 50 bars — signals never warm up (MA needs 100)
    auto bars = make_bars("ES", std::vector<double>(50, 100.0));
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    // positions vector has 49 entries (t+1 loop) but all with position = 0
    for (const auto& dp : positions) assert(approx(dp.position, 0.0));
    assert(approx(metrics.total_return, 0.0));

    std::cout << "PASS  test_insufficient_bars_no_active_days\n";
}

static void test_label_is_propagated() {
    auto bars = make_bars("ES", std::vector<double>(10, 100.0));
    Backtester bt;
    auto [positions, metrics] = bt.run(bars, "in_sample");

    assert(metrics.label == "in_sample");

    std::cout << "PASS  test_label_is_propagated\n";
}

static void test_uptrend_generates_positive_positions() {
    // Rising price series: MA20 > MA100 and momentum > 0 once warmed up
    auto bars = make_trending_bars(100.0, 1.0, 130);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    // Find the first day where position != 0
    bool found_nonzero = false;
    for (const auto& dp : positions) {
        if (!approx(dp.position, 0.0)) {
            assert(dp.position > 0.0);  // uptrend => long
            found_nonzero = true;
            break;
        }
    }
    assert(found_nonzero);

    std::cout << "PASS  test_uptrend_generates_positive_positions\n";
}

static void test_downtrend_generates_negative_positions() {
    // Falling price: MA20 < MA100, momentum < 0 => short
    auto bars = make_trending_bars(230.0, -1.0, 130);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    bool found_nonzero = false;
    for (const auto& dp : positions) {
        if (!approx(dp.position, 0.0)) {
            assert(dp.position < 0.0);  // downtrend => short
            found_nonzero = true;
            break;
        }
    }
    assert(found_nonzero);

    std::cout << "PASS  test_downtrend_generates_negative_positions\n";
}

static void test_constant_prices_zero_pnl() {
    // Flat prices => all returns = 0 => PnL = 0 regardless of position
    auto bars = make_bars("ES", std::vector<double>(130, 100.0));
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    for (const auto& dp : positions) assert(approx(dp.pnl, 0.0));
    assert(approx(metrics.total_return, 0.0));
    assert(approx(metrics.sharpe, 0.0));

    std::cout << "PASS  test_constant_prices_zero_pnl\n";
}

static void test_uptrend_positive_total_return() {
    // Rising price => long position => positive log returns => positive PnL
    auto bars = make_trending_bars(100.0, 1.0, 150);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    assert(metrics.total_return > 0.0);

    std::cout << "PASS  test_uptrend_positive_total_return\n";
}

static void test_downtrend_positive_total_return() {
    // Falling price => short position => positive PnL (shorting a falling market)
    auto bars = make_trending_bars(300.0, -1.0, 150);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    assert(metrics.total_return > 0.0);

    std::cout << "PASS  test_downtrend_positive_total_return\n";
}

static void test_num_days_correct() {
    // 130 bars => 129 position entries (loop: t from 0 to 128)
    auto bars = make_trending_bars(100.0, 0.5, 130);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    assert(metrics.num_days == 129);
    assert(static_cast<int>(positions.size()) == 129);

    std::cout << "PASS  test_num_days_correct\n";
}

static void test_hit_ratio_in_range() {
    auto bars = make_trending_bars(100.0, 1.0, 150);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    assert(metrics.hit_ratio >= 0.0 && metrics.hit_ratio <= 1.0);

    std::cout << "PASS  test_hit_ratio_in_range\n";
}

static void test_max_drawdown_nonpositive() {
    auto bars = make_trending_bars(100.0, 1.0, 150);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    // Max drawdown is always <= 0 by definition (peak minus trough)
    assert(metrics.max_drawdown <= 0.0);

    std::cout << "PASS  test_max_drawdown_nonpositive\n";
}

static void test_turnover_nonnegative() {
    auto bars = make_trending_bars(100.0, 1.0, 150);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    assert(metrics.turnover >= 0.0);

    std::cout << "PASS  test_turnover_nonnegative\n";
}

static void test_custom_signal_params() {
    // Use tighter windows so warmup is shorter (fast=5, slow=20, lb=15, skip=2)
    Backtester bt{5, 20, 15, 2};
    auto bars = make_trending_bars(100.0, 1.0, 60);
    auto [positions, metrics] = bt.run(bars);

    // With slow=20 warmup finishes by bar 20; should have active positions
    bool found_nonzero = false;
    for (const auto& dp : positions) {
        if (!approx(dp.position, 0.0)) { found_nonzero = true; break; }
    }
    assert(found_nonzero);

    std::cout << "PASS  test_custom_signal_params\n";
}

static void test_pnl_eod_timing() {
    // Verify: position on day t drives PnL on day t+1.
    // We use 3 bars where only the last return matters.
    // With tiny bar count signals won't warm up, so position = 0 and pnl = 0.
    // This test just asserts structural invariant: pnl[i] = position[i] * return[i+1].
    auto bars = make_trending_bars(100.0, 5.0, 130);
    Backtester bt;
    auto [positions, metrics] = bt.run(bars);

    for (std::size_t i = 0; i < positions.size(); ++i) {
        // bars[i+1].return_1d corresponds to return on bar index i+1
        const double expected_pnl = positions[i].position * bars[i + 1].return_1d;
        assert(approx(positions[i].pnl, expected_pnl, 1e-12));
    }

    std::cout << "PASS  test_pnl_eod_timing\n";
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "Running Backtester tests...\n\n";

    test_empty_bars_returns_zero_metrics();
    test_insufficient_bars_no_active_days();
    test_label_is_propagated();
    test_uptrend_generates_positive_positions();
    test_downtrend_generates_negative_positions();
    test_constant_prices_zero_pnl();
    test_uptrend_positive_total_return();
    test_downtrend_positive_total_return();
    test_num_days_correct();
    test_hit_ratio_in_range();
    test_max_drawdown_nonpositive();
    test_turnover_nonnegative();
    test_custom_signal_params();
    test_pnl_eod_timing();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
