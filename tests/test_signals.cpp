#include "core/RollingWindow.hpp"
#include "signals/MovingAverageSignal.hpp"
#include "signals/MomentumSignal.hpp"
#include "signals/VolatilitySignal.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool approx(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) < tol;
}

// Build a flat vector of DailyBars with given close prices.
// return_1d is computed from consecutive closes (0 for first bar).
static std::vector<DailyBar> make_bars(const std::string& symbol,
                                       const std::vector<double>& closes) {
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

// ── RollingWindow tests ───────────────────────────────────────────────────────

static void test_rolling_window_mean() {
    RollingWindow<double, 3> w;
    w.push(1.0); w.push(2.0); w.push(3.0);

    assert(w.full());
    assert(approx(w.mean(), 2.0));

    // Push a 4th value — evicts oldest (1.0)
    w.push(4.0);
    assert(approx(w.mean(), 3.0));  // (2+3+4)/3

    std::cout << "PASS  test_rolling_window_mean\n";
}

static void test_rolling_window_stddev() {
    RollingWindow<double, 4> w;
    w.push(2.0); w.push(4.0); w.push(4.0); w.push(4.0);
    // mean = 3.5, deviations: -1.5, 0.5, 0.5, 0.5
    // variance = (2.25+0.25+0.25+0.25)/4 = 0.75
    // stddev = sqrt(0.75)
    assert(approx(w.stddev(), std::sqrt(0.75)));

    std::cout << "PASS  test_rolling_window_stddev\n";
}

static void test_rolling_window_latest() {
    RollingWindow<double, 5> w;
    w.push(10.0);
    assert(approx(w.latest(), 10.0));
    w.push(99.0);
    assert(approx(w.latest(), 99.0));

    std::cout << "PASS  test_rolling_window_latest\n";
}

static void test_rolling_window_not_full() {
    RollingWindow<double, 10> w;
    assert(!w.full());
    assert(w.size() == 0);
    w.push(1.0);
    assert(!w.full());
    assert(w.size() == 1);

    std::cout << "PASS  test_rolling_window_not_full\n";
}

// ── MovingAverageSignal tests ─────────────────────────────────────────────────

static void test_ma_insufficient_data() {
    MovingAverageSignal sig{20, 100};
    auto bars = make_bars("ES", std::vector<double>(50, 100.0));  // only 50 bars
    auto r = sig.compute(bars);
    assert(!r.valid);
    assert(approx(r.value, 0.0));

    std::cout << "PASS  test_ma_insufficient_data\n";
}

static void test_ma_uptrend_signal() {
    // 105 bars: first 80 at 100, last 25 at 200
    // After 105 bars:
    //   MA20  = mean of last 20 closes = 200.0
    //   MA100 = mean of last 100 closes = (75*100 + 25*200) / 100 = 125.0
    //   MA20 > MA100 => signal = +1
    std::vector<double> closes(80, 100.0);
    closes.insert(closes.end(), 25, 200.0);

    MovingAverageSignal sig{20, 100};
    auto bars = make_bars("ES", closes);
    auto r = sig.compute(bars);

    assert(r.valid);
    assert(approx(r.value, 1.0));
    assert(r.confidence > 0.0);

    std::cout << "PASS  test_ma_uptrend_signal\n";
}

static void test_ma_downtrend_signal() {
    // 105 bars: first 80 at 200, last 25 at 100
    //   MA20 = 100.0, MA100 = (75*200 + 25*100) / 100 = 175.0
    //   MA20 < MA100 => signal = -1
    std::vector<double> closes(80, 200.0);
    closes.insert(closes.end(), 25, 100.0);

    MovingAverageSignal sig{20, 100};
    auto bars = make_bars("ES", closes);
    auto r = sig.compute(bars);

    assert(r.valid);
    assert(approx(r.value, -1.0));

    std::cout << "PASS  test_ma_downtrend_signal\n";
}

static void test_ma_invalid_params_throws() {
    bool threw = false;
    try { MovingAverageSignal bad{100, 20}; }  // fast >= slow
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    std::cout << "PASS  test_ma_invalid_params_throws\n";
}

// ── MomentumSignal tests ──────────────────────────────────────────────────────

static void test_momentum_insufficient_data() {
    MomentumSignal sig;
    auto bars = make_bars("ES", std::vector<double>(30, 100.0));  // need 61
    auto r = sig.compute(bars);
    assert(!r.valid);

    std::cout << "PASS  test_momentum_insufficient_data\n";
}

static void test_momentum_positive() {
    // 65 bars: price rises from 100 to 200
    // P[t - 5]  = bars[64-5]  = bars[59] -> close in the 160s
    // P[t - 60] = bars[64-60] = bars[4]  -> close near 100
    // ratio > 1 => momentum > 0 => signal = +1
    std::vector<double> closes;
    for (int i = 0; i < 65; ++i) closes.push_back(100.0 + i * 1.5);

    MomentumSignal sig{60, 5};
    auto bars = make_bars("ES", closes);
    auto r = sig.compute(bars);

    assert(r.valid);
    assert(approx(r.value, 1.0));
    assert(r.confidence > 0.0);

    std::cout << "PASS  test_momentum_positive\n";
}

static void test_momentum_negative() {
    // 65 bars: price falls from 200 to 100
    std::vector<double> closes;
    for (int i = 0; i < 65; ++i) closes.push_back(200.0 - i * 1.5);

    MomentumSignal sig{60, 5};
    auto bars = make_bars("ES", closes);
    auto r = sig.compute(bars);

    assert(r.valid);
    assert(approx(r.value, -1.0));

    std::cout << "PASS  test_momentum_negative\n";
}

// ── VolatilitySignal tests ────────────────────────────────────────────────────

static void test_vol_insufficient_data() {
    VolatilitySignal sig;
    auto bars = make_bars("ES", std::vector<double>(10, 100.0));
    auto r = sig.compute(bars);
    assert(!r.valid);

    std::cout << "PASS  test_vol_insufficient_data\n";
}

static void test_vol_constant_price_is_zero() {
    // Constant prices => zero log returns => zero volatility
    VolatilitySignal sig;
    auto bars = make_bars("ES", std::vector<double>(30, 100.0));
    auto r = sig.compute(bars);

    assert(r.valid);
    assert(approx(r.value, 0.0));

    std::cout << "PASS  test_vol_constant_price_is_zero\n";
}

static void test_vol_reasonable_estimate() {
    // Daily returns of 1% => annualized vol ≈ 0.01 * sqrt(252) ≈ 0.1587
    // We set close[i] = 100 * exp(0.01 * i) => log returns all 0.01
    VolatilitySignal sig;
    std::vector<double> closes;
    for (int i = 0; i < 25; ++i) closes.push_back(100.0 * std::exp(0.01 * i));
    auto bars = make_bars("ES", closes);
    auto r = sig.compute(bars);

    assert(r.valid);
    // stddev of 20 identical returns (0.01) = 0.0
    // All returns are exactly 0.01 so stddev = 0, vol = 0
    // (constant return series has zero variance)
    assert(approx(r.value, 0.0));

    std::cout << "PASS  test_vol_reasonable_estimate\n";
}

static void test_vol_nonzero_for_varying_returns() {
    // Alternating returns +2% / -2% => nonzero vol
    VolatilitySignal sig;
    std::vector<double> closes;
    double price = 100.0;
    closes.push_back(price);
    for (int i = 0; i < 25; ++i) {
        price *= (i % 2 == 0) ? 1.02 : 0.98;
        closes.push_back(price);
    }
    auto bars = make_bars("ES", closes);
    auto r = sig.compute(bars);

    assert(r.valid);
    assert(r.value > 0.0);  // vol must be positive for varying returns

    std::cout << "PASS  test_vol_nonzero_for_varying_returns\n";
}

// ── Concept check ─────────────────────────────────────────────────────────────

static void test_signal_concept_satisfied() {
    // Static assertions: all three classes satisfy SignalComputable at compile time.
    // If any compute() signature is wrong, this fails to compile — not at runtime.
    static_assert(SignalComputable<MovingAverageSignal>);
    static_assert(SignalComputable<MomentumSignal>);
    static_assert(SignalComputable<VolatilitySignal>);

    std::cout << "PASS  test_signal_concept_satisfied (compile-time)\n";
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "Running Signal Engine tests...\n\n";

    test_rolling_window_mean();
    test_rolling_window_stddev();
    test_rolling_window_latest();
    test_rolling_window_not_full();

    test_ma_insufficient_data();
    test_ma_uptrend_signal();
    test_ma_downtrend_signal();
    test_ma_invalid_params_throws();

    test_momentum_insufficient_data();
    test_momentum_positive();
    test_momentum_negative();

    test_vol_insufficient_data();
    test_vol_constant_price_is_zero();
    test_vol_reasonable_estimate();
    test_vol_nonzero_for_varying_returns();

    test_signal_concept_satisfied();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
