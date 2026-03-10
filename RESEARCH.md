# RESEARCH.md
**Multithreaded Quant Research Engine**

---

## 1. Research Philosophy

This document defines the research conventions, signal specifications, portfolio rules, and experiment system for the backtesting framework.

All experiments must be:
- **reproducible** — same config produces same results
- **free from look-ahead bias** — data available at decision time, not later
- **internally consistent** — one canonical definition per concept
- **clearly documented** — assumptions fixed and stated here

The goal is not to produce the optimal strategy, but to provide a **clear and defensible research framework** for a multithreaded quant research engine.

---

## 2. Asset Universe

The baseline universe is **9 liquid macro futures** representing major cross-asset classes. Daily data from June 2010 (Databento GLBX.MDP3 availability).

```
Equities
--------
ES   S&P 500 futures
NQ   NASDAQ 100 futures
YM   Dow Jones futures

Rates
-----
ZN   10-Year Treasury futures
ZB   30-Year Treasury futures

Commodities
-----------
CL   Crude Oil futures
GC   Gold futures

FX
--
6E   Euro futures
6J   Japanese Yen futures
```

This cross-asset universe allows macro signals to interact across markets — equity, rates, commodities, and FX all respond differently to regime transitions.

All references to "the universe" throughout this project mean this baseline macro futures universe unless a sweep explicitly defines a subset.

---

## 3. Data and Backtest Timing

### EOD Rule

All signals are generated using **end-of-day data**. The engine enforces:

```
features(t) → signal(t) → trade(t+1)
```

Features computed at day `t` close. Portfolio weights applied at day `t+1` open. Returns realized from `t+1` onward.

In code:

```
features[t]
signal[t]      = model(features[t])
position[t+1]  = signal[t]
```

### Point-in-Time Taxonomy

| Variable type | Revision risk | Publication lag | Safe for backtest |
|---------------|---------------|-----------------|-------------------|
| Treasury yields (DGS2, DGS10) | none | none | yes |
| Breakeven inflation (T10YIE) | none | none | yes |
| Credit spreads (BAA10Y) | minimal | none | yes |
| VIX | none | none | yes |
| Dollar index (DTWEXBGS) | none | none | yes |
| Financial stress index (STLFSI4) | minimal | small | mostly yes |
| Fed balance sheet (WALCL) | none | ~1 week | track `available_from` |

**Market-based series** (yields, breakevens, spreads, VIX, dollar) are point-in-time by construction — they are traded prices that never change historically.

**Constructed/lagged series** (STLFSI4, WALCL) carry a small publication lag. The pipeline records `available_from` alongside the observation date so the engine can enforce correct timing. See `data_ingestion.md §3` for struct definitions.

---

## 4. Out-of-Sample Evaluation

### 70/30 Fixed Split

The backtest period (2010 onward) is split into:

| Partition | Dates | Use |
|-----------|-------|-----|
| In-sample (IS) | 2010-01-01 — approx. 2018-12-31 | parameter selection, signal development |
| Out-of-sample (OOS) | approx. 2019-01-01 — present | evaluation only |

The exact split date is defined in the experiment config and fixed for all experiments. No re-using OOS data to tune parameters.

**No walk-forward in the baseline.** Walk-forward cross-validation is a future extension. The baseline uses a single fixed split to keep the results interpretable and the implementation simple.

**Rule:** signal parameters and portfolio rules are chosen using IS data only. OOS performance is reported as the primary evaluation metric.

---

## 5. Signal Definitions

All three baseline signals are intentionally simple and widely used in academic literature. Parameters below are **canonical baseline values**. Parameter sweeps reference these as defaults.

### 5.1 Moving Average Crossover (Trend)

```
Fast window  = 20 trading days
Slow window  = 100 trading days

MA_fast(t)   = average(P[t-19 : t])
MA_slow(t)   = average(P[t-99 : t])

signal(t) = +1  if MA_fast > MA_slow
            -1  if MA_fast < MA_slow
```

Captures medium-term trend-following behavior.

### 5.2 Momentum Signal

```
Lookback window = 60 trading days
Skip window     = 5 trading days

momentum(t) = P[t-5] / P[t-60] - 1

signal(t) = +1  if momentum > 0
            -1  if momentum < 0
```

The 5-day skip avoids contamination from short-term mean reversion. Captures intermediate-term price continuation.

### 5.3 Volatility Estimate

```
Lookback window = 20 trading days

r[t]     = log(P[t] / P[t-1])      (on Panama-adjusted series)
σ20(t)   = sqrt(252) * std(r[t-19 : t])
```

Volatility is used **only for risk scaling** in the portfolio construction step — not as a standalone directional signal.

---

## 6. Portfolio Construction

### Inverse Volatility Scaling

For asset `i` at time `t`:

```
raw_weight_i(t) = signal_i(t) / σ20_i(t)
```

Normalize so total gross exposure equals 1:

```
weight_i(t) = raw_weight_i(t) / Σ_j |raw_weight_j(t)|
```

Result: `Σ_i |weight_i(t)| = 1`

Assets with higher volatility receive smaller allocations; assets with lower volatility receive larger allocations. This approximates **equal risk contribution**.

### Rebalancing

Portfolio rebalanced **daily**.

```
t close:   compute signals
t+1 open:  rebalance portfolio
```

### Transaction Costs (Baseline)

```
transaction_cost = 0
```

Acceptable for an academic prototype. Future extensions: fixed bps cost, spread cost, slippage model.

---

## 7. Performance Metrics

All metrics computed from the realized equity curve.

```
Return_annual     = mean(R_daily) * 252
Vol_annual        = std(R_daily) * sqrt(252)
Sharpe            = Return_annual / Vol_annual      (risk-free = 0 in baseline)
Max_Drawdown      = max over t of (peak(t) - trough(t)) / peak(t)
Hit_Ratio         = fraction of days with positive daily return
Turnover          = mean daily change in gross position
```

**Risk-free rate:** baseline uses `rf = 0` (stated explicitly). Future version: daily 3M T-bill proxy from FRED.

```
Sharpe_v2 = mean(R_daily - rf_daily / 252) / std(R_daily) * sqrt(252)
```

---

## 8. Macro Feature Set

### FRED Series

The daily macro state vector uses 10 features from FRED + CBOE:

| Feature | Series | Description |
|---------|--------|-------------|
| x1 | DGS2 | 2-year Treasury yield (short-rate anchor) |
| x2 | DGS10 | 10-year Treasury yield (long-rate environment) |
| x3 | T10Y3M | 10Y−3M spread (recession proxy) |
| x4 | Δ20(T10Y3M) | 20-day change in curve slope (steepening/flattening) |
| x5 | T10YIE | 10-year breakeven inflation |
| x6 | DFII10 | 10-year TIPS real yield (or DGS10 − T10YIE) |
| x7 | BAA10Y | Baa corporate spread over 10Y Treasury (credit stress) |
| x8 | log(VIX) | Equity vol / risk aversion |
| x9 | STLFSI4 | St. Louis Fed Financial Stress Index |
| x10 | DTWEXBGS | Nominal broad U.S. dollar index |

Optional 11th: `log(WALCL)` or its 13-week change (Fed balance sheet size).

### Transformation Stack

Raw series enter the model as **standardized transformed features**. For each series `x_i`:

```
z_i(t) = (x_i(t) - μ_i(t, 252)) / σ_i(t, 252)
```

where `μ` and `σ` are rolling 1-year mean and standard deviation.

For trending quantities, also compute:

```
Δ20 x_i(t) = x_i(t) - x_i(t-20)
```

This produces the daily macro state vector:

```
Z_t = [z(DGS2), z(DGS10), z(T10Y3M), z(Δ20 T10Y3M),
       z(T10YIE), z(DFII10), z(BAA10Y), z(log VIX),
       z(STLFSI4), z(DTWEXBGS)]^T
```

---

## 9. Regime Model

### Four Latent Regimes

```
R_t ∈ {1, 2, 3, 4}

1 = Risk-on / disinflationary growth
2 = Inflation / reflation
3 = Growth scare / slowdown
4 = Stress / crisis
```

### Score-Based Regime Model (Baseline)

Build one score per regime as a weighted linear combination of the macro state vector:

```
S_k(t) = a_k + w_k^T Z_t
```

Example hand-designed weights:

```
S_risk-on    = -0.5 z_VIX  - 0.5 z_credit  - 0.4 z_FSI  + 0.3 z_Δslope  - 0.2 z_USD
S_inflation  = +0.6 z_BE   + 0.5 z_realchg  + 0.3 z_10Y  - 0.2 z_slope   + 0.2 z_USD
S_slowdown   = -0.5 z_slope - 0.4 z_Δslope  + 0.3 z_credit + 0.2 z_VIX
S_stress     = +0.7 z_VIX  + 0.7 z_FSI     + 0.6 z_credit - 0.3 z_slope  + 0.2 z_USD
```

Convert scores to regime probabilities via softmax:

```
P(R_t = k | Z_t) = exp(S_k(t)) / Σ_j exp(S_j(t))
```

This is transparent, explainable, and easy to implement in C++.

### HMM Extension (Optional)

The next level is a Hidden Markov Model where the regime is latent and persistent:

```
P(R_t = j | R_{t-1} = i) = A_ij        (transition matrix)
Z_t | (R_t = k) ~ N(μ_k, Σ_k)          (emission model)
```

Inference via Baum-Welch forward pass to get filtered probabilities `γ_k(t) = P(R_t = k | Z_{1:t})`.

### Regime-Weighted Signal Combination

Given three signal streams, combine using regime probabilities:

```
α_t = p1(t) * α_trend  +  p2(t) * α_carry  +  (p3(t) + p4(t)) * α_mr
```

In high-stress states, mechanically reduce risk:

```
w_t = c_t * α_t / σ̂_t
c_t = 1 - 0.5 * p4(t)
```

### Carry Signal

Carry signal (`α_carry`) is explicitly deferred to a later version. The baseline implements trend and mean reversion only. The regime formula above uses `p2(t) * α_carry` as a placeholder.

---

## 10. Model Families

Three model families are implemented.

### Trend Model
- Moving average crossover (MA20/MA100)
- Time-series momentum (60-day, 5-day skip)
- Volatility-scaled signal

### Mean Reversion / Statistical Model
- Z-score on returns
- Short-term reversal
- Rolling regression or ridge combination of features

### Regime-Aware Model
- Score-based regime classifier over macro state vector `Z_t`
- Switch between trend and mean-reversion logic based on regime
- Optional: HMM regime filter

**Not included:** deep learning models. These would dilute the C++ systems story.

---

## 11. Model Framework — Future Layers

The model layer evolves from rule-based to statistical to ensemble:

**Layer 1 (baseline):** rule-based signals (MA crossover, momentum, volatility filter, regime filter)

**Layer 2 (statistical):** rolling z-score mean reversion, linear regression forecasts, ridge/lasso signal combination, AR models on returns, PCA for dimensionality reduction

**Layer 3 (cross-sectional):** momentum ranking, carry ranking, risk-adjusted carry, factor composite score

**Layer 4 (regime-aware):** regime-conditional signal weighting, HMM state estimation

**Layer 5 (ensemble):** weighted combination of trend + carry + valuation + vol penalty + macro regime adjustment

The pipeline thinking is:

```
data → features → models → forecasts → portfolio decisions → execution assumptions → evaluation
```

---

## 12. Experiment Configuration

### Philosophy

An experiment is a **fully specified, immutable research run**. Each experiment answers:

- what data and universe was used
- what date range
- what features were computed
- what model was run
- what parameters were chosen
- how the portfolio was constructed
- what evaluation metrics were recorded

Once launched, it has a unique ID and a fixed configuration. No result should exist without a config file.

### Config Value Types

Config parameter values use `std::variant` so a single `ConfigValue` type holds integers, doubles, strings, or booleans without a separate type per field:

```cpp
using ConfigValue = std::variant<int, double, std::string, bool>;

struct ExperimentConfig {
    std::unordered_map<std::string, ConfigValue> params;
};

// Reading a typed value with structured bindings:
auto get_window = [&](const std::string& key) {
    return std::get<int>(config.params.at(key));
};
int ma_fast = get_window("ma_fast");  // 20
```

`std::visit` can dispatch on the active type for serialization, validation, or sweep generation without casting.

### Required Configuration Fields

**Metadata:** `experiment_name`, `experiment_id`, `description`, `author`, `created_at`, `random_seed`

**Data:** `market_data_source`, `macro_data_source`, `start_date`, `end_date`, `frequency`, `universe`

**Features:** `feature_set`, `lookback_windows`, `normalization_method`, `macro_join_method`, `missing_data_policy`

**Model:** `model_type`, `model_parameters`, `signal_combination_rule`, `regime_model_enabled`, `regime_parameters`

**Portfolio:** `position_sizing_method`, `volatility_lookback`, `gross_exposure_target`, `net_exposure_target`, `rebalance_frequency`, `transaction_cost_bps`

**Evaluation:** `risk_free_rate_assumption`, `benchmark`, `metrics`, `save_equity_curve`, `save_positions`, `save_signals`

### Baseline YAML

```yaml
experiment_name: baseline_trend_daily
experiment_id: auto_generated
description: Trend-following baseline on daily macro futures
random_seed: 42

data:
  market_data_source: databento
  macro_data_source: fred
  start_date: 2010-01-01
  end_date: latest
  frequency: daily
  universe: [ES, NQ, YM, ZN, ZB, CL, GC, 6E, 6J]

features:
  returns_lookback: 60
  vol_lookback: 20
  ma_fast: 20
  ma_slow: 100
  normalization_method: rolling_zscore_252
  macro_join_method: forward_fill_after_release
  missing_data_policy: drop_if_critical

model:
  model_type: trend
  model_parameters:
    signal: moving_average_crossover
  signal_combination_rule: single_signal
  regime_model_enabled: false

portfolio:
  position_sizing_method: inverse_volatility
  volatility_lookback: 20
  gross_exposure_target: 1.0
  rebalance_frequency: daily
  transaction_cost_bps: 0

evaluation:
  risk_free_rate_assumption: zero
  benchmark: equal_weight_buy_and_hold
  metrics: [annual_return, annual_volatility, sharpe, max_drawdown, turnover, hit_ratio]
  save_equity_curve: true
  save_positions: true
  save_signals: true
```

### Parameter Sweeps

Sweeps generate N experiments, each treated as a separate immutable experiment instance:

**Sweep 1 — Trend signal lookbacks:**
```
ma_fast ∈ {10, 20, 50}  ×  ma_slow ∈ {50, 100, 200}  ×  vol_lookback ∈ {20, 40}
→ 18 experiments
```

**Sweep 2 — Momentum:**
```
momentum_lookback ∈ {20, 60, 120}  ×  skip_window ∈ {0, 5}  ×  vol_lookback ∈ {20, 40}
→ 12 experiments
```

**Sweep 3 — Regime overlay:**
```
regime_model ∈ {off, on}  ×  stress_threshold ∈ {low, medium, high}
→ 6 experiments
```

The first sweep should vary one model family at a time. Do not sweep data cleaning rules or multiple conflicting universes simultaneously.

The sweep generator uses lambdas to build experiment lists without defining named functions for one-off transformations:

```cpp
// Build all combinations for sweep 1
std::vector<ExperimentConfig> configs;
for (int fast : {10, 20, 50})
    for (int slow : {50, 100, 200})
        for (int vol : {20, 40}) {
            auto cfg = base_config;
            cfg.params["ma_fast"]     = fast;
            cfg.params["ma_slow"]     = slow;
            cfg.params["vol_lookback"] = vol;
            configs.push_back(cfg);
        }

// Submit all to thread pool with a capturing lambda
auto futures = configs | std::views::transform([&pool](const auto& cfg) {
    return pool.submit([cfg] { return run_experiment(cfg); });
});
```

### Multithreading

Thread pool at the experiment level:

```text
Thread 1 -> Experiment A
Thread 2 -> Experiment B
Thread 3 -> Experiment C
```

For version one: experiment-level parallelism first, then feature computation parallelism by instrument.

### Experiment ID Convention

```
<model_type>_<universe_tag>_<startdate>_<hash>
e.g.: trend_macro_20100101_a81f29
```

### Output Artifacts

```
results/experiments/<experiment_id>/
    config.json
    metrics.json
    equity_curve.csv
    positions.csv
    signals.csv
    summary.txt
```

### Failure Handling

An experiment may fail (missing data, invalid parameters, divide-by-zero in vol scaling, file system errors). The engine must never crash the full sweep:

```
failed experiment → status = FAILED, error message recorded, sweep continues
```

### Reproducibility Rules

- every experiment saves its exact config
- all random behavior uses the recorded seed
- all data ranges are explicit in the config
- all outputs are associated with a single experiment ID

---

## 13. Research Assumptions Summary

All fixed baseline values in one place:

| Parameter | Value |
|-----------|-------|
| Data frequency | Daily |
| Backtest start | 2010-06-06 (Databento GLBX.MDP3 availability) |
| Universe | ES, NQ, YM, ZN, ZB, CL, GC, 6E, 6J (9 instruments) |
| MA fast window | 20 trading days |
| MA slow window | 100 trading days |
| Momentum lookback | 60 trading days |
| Momentum skip | 5 trading days |
| Volatility lookback | 20 trading days |
| Volatility annualization | sqrt(252) |
| Portfolio sizing | Inverse volatility |
| Gross exposure | 1.0 (normalized) |
| Rebalance frequency | Daily |
| Transaction costs | 0 bps (baseline) |
| Risk-free rate | 0 (baseline) |
| Sharpe annualization | sqrt(252) |
| Continuous futures | Panama additive back-adjustment |
| Roll trigger | Volume crossover; fallback 5 days before expiry |
| OOS split | 70% IS / 30% OOS, fixed (no walk-forward in baseline) |
| Approximate OOS start | 2019-01-01 |
| Random seed | 42 |
