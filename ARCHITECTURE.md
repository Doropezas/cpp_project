# ARCHITECTURE.md
**Multithreaded Quant Research Engine**

---

## 1. High-Level System Overview

The engine has five main modules that interact explicitly and in one direction.

```text
+------------------------------------------------------------------+
|                 MULTITHREADED QUANT RESEARCH ENGINE              |
|                      (Daily Macro Futures)                       |
+------------------------------------------------------------------+

   Databento (local DBN)                  FRED + VIX (local CSV)
   ---------------------                  ----------------------
   ohlcv-1d, continuous series            yields, spreads, vol, FSI
            |                                          |
            +------------------+------------------------+
                               v
                    +---------------------+
                    |  MARKET DATA LOADER  |
                    |  reads local files   |
                    |  produces DailyBar   |
                    +---------+-----------+
                               | vector<DailyBar>
                               v
                    +---------------------+
                    |     THREAD POOL      |
                    |  task submission     |
                    |  std::future results |
                    +---------+-----------+
                               |
              +----------------+----------------+
              v                                 v
   +---------------------+         +---------------------+
   |   SIGNAL ENGINE      |         |  BACKTEST UTILITIES  |
   |  features & signals  |         |  positions, returns  |
   +---------+-----------+         +---------+-----------+
              +----------------+----------------+
                               v
                    +---------------------+
                    |  RESULT AGGREGATOR   |
                    |  metrics + summaries |
                    +---------+-----------+
                               v
                    +---------------------+
                    |  report / CSV / JSON |
                    +---------------------+
```

**Execution model:** load data → group by symbol → submit one signal+backtest job per symbol → gather futures → aggregate.

---

## 2. Data Flow + Pipeline Layers

```text
[1] DATA SOURCES
    local data/processed/continuous/  (Panama-adjusted bars)
    local data/processed/macro/       (joined macro state)
                              |
                              v
[2] DATA INGESTION
    parse DBN / CSV, validate schema, normalize timestamps
    (See data_ingestion.md for full spec)
                              |
                              v
[3] DATA NORMALIZATION
    symbol mapping, continuous series build (Panama method)
    calendar alignment, missing data policy
                              |
                              v
[4] FEATURE ENGINEERING
    returns, rolling vol, momentum, z-scores, macro joins
    drawdowns, regime features
                              |
                              v
[5] MODEL LAYER
    TrendModel, MeanReversionModel, RegimeSwitchingModel
                              |
                              v
[6] PORTFOLIO CONSTRUCTION
    inverse-vol sizing, normalization, exposure caps
                              |
                              v
[7] BACKTEST ENGINE
    daily rebalance, PnL accounting, cost assumptions
                              |
                              v
[8] EVALUATION / REPORTS
    Sharpe, max drawdown, hit ratio, turnover, regime breakdown
```

---

## 3. Module Responsibilities

### 3.1 Market Data Loader

Reads local processed files, validates schema, parses values, returns typed objects.

```cpp
class MarketDataLoader {
public:
    std::vector<DailyBar> load_directory(const std::string& path);
    std::vector<DailyBar> load_file(const std::string& path);
};
```

This module is single-purpose and mostly synchronous. It does not compute signals.

In the current implementation, bars are loaded once by the main thread and passed to worker threads by value (moved into lambda captures). A `MarketDataCache` with `std::shared_mutex` for concurrent read access is a planned future extension (see §13).

### 3.2 Thread Pool

Owns worker threads, accepts tasks, returns futures to callers, shuts down gracefully.

```cpp
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads);
    ~ThreadPool();  // joins all threads — RAII ownership

    // Deleted: ThreadPool is not copyable or movable
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    // Perfect-forwarding submit: accepts any callable + args,
    // returns a future of the callable's return type
    template<class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

private:
    ThreadSafeQueue<std::function<void()>> tasks_;  // type-erased task storage
    std::vector<std::jthread> workers_;   // auto-joins on destruction (C++20)
};
```

Uses real `std::jthread` (C++20) via Homebrew GCC 15 (`/opt/homebrew/bin/g++-15`). Apple's Clang ships an incomplete `libc++` that lacks `std::jthread`; the CMakeLists.txt sets `CMAKE_CXX_COMPILER` to GCC 15 explicitly.

Key course concepts demonstrated here:
- **Rule of five**: destructor defined → copy/move explicitly deleted
- **`std::jthread` / RAII**: auto-joins on destruction — no manual `join()`, no leaked threads
- **`std::function`**: type erasure — queue holds any callable regardless of signature
- **Perfect forwarding**: `F&&` + `Args&&...` + `std::forward` preserves value categories
- **`std::future` / `std::packaged_task`**: async result passing between threads

The thread pool does not know anything about finance logic.

### 3.3 Signal Engine

Computes reusable indicators and transforms them into trading signals. Does not own threads.

```cpp
struct SignalResult {
    std::string symbol;
    double value{0.0};       // directional signal: +1 (long), -1 (short)
    double confidence{0.0};  // signal strength [0, 1]
    bool   valid{false};     // false during warmup period
};

// C++20 Concept: constrains any class that implements compute()
template<typename T>
concept SignalComputable = requires(const T& s, std::span<const DailyBar> bars) {
    { s.compute(bars) } -> std::same_as<SignalResult>;
};

class MovingAverageSignal { /* fast/slow MA crossover */ };
class MomentumSignal      { /* lookback/skip price ratio */ };
class VolatilitySignal    { /* 20-day realized vol, annualized */ };
```

All three classes satisfy `SignalComputable` — verified via `static_assert` in the test suite.

Callers decompose results with structured bindings:

```cpp
auto r = signal.compute(bars);
if (r.valid) {
    // r.value, r.confidence available
}
```

Signal parameters default to the `constexpr` baseline values in `Constants.hpp`.

### 3.4 Backtester

Converts signal outputs into positions, applies position rules, computes returns, tracks portfolio evolution.

- signal > 0 → long; signal < 0 → short; signal = 0 → flat
- daily rebalance by default
- later extensions: transaction costs, slippage, leverage, cash accounting

### 3.5 Result Aggregator

Combines results from many parallel jobs, computes summary statistics, produces stable output format.

Metrics: total return, annualized return, volatility, Sharpe ratio, max drawdown, win rate, turnover.

The aggregator holds two mutexes (results store + log writer). Uses `std::scoped_lock` to acquire both atomically and avoid deadlock:

```cpp
void record(BacktestResult result) {
    std::scoped_lock lck(results_mutex_, log_mutex_);  // deadlock-safe
    results_.push_back(std::move(result));
    log_ << result.symbol << " recorded\n";
}
```

---

## 4. Core Data Structures

```cpp
struct MarketEvent {
    std::string symbol;
    std::string date;
    double open{};
    double high{};
    double low{};
    double close{};
    double volume{};
};

struct DailyBar {
    std::string symbol;
    std::string date;
    double open{};
    double high{};
    double low{};
    double close{};          // Panama-adjusted
    double close_unadjusted{};
    double volume{};
    double return_1d{};      // log return on adjusted series
    bool is_roll_date{false};
};

struct PerformanceMetrics {
    std::string label;          // experiment ID or symbol name
    double sharpe{0.0};         // annualized Sharpe ratio
    double max_drawdown{0.0};   // peak-to-trough drawdown (≤ 0)
    double hit_ratio{0.0};      // fraction of days with positive PnL
    double turnover{0.0};       // mean daily absolute position change
    double total_return{0.0};   // cumulative log-return over the period
    int    num_days{0};
};

// Backtester::RunResult — returned by Backtester::run()
struct RunResult {
    std::vector<DailyPosition> positions;  // per-day position log
    PerformanceMetrics         metrics;
};
```

### RollingWindow

A reusable fixed-size circular buffer used by all signal computations:

```cpp
template<typename T, std::size_t N>
class RollingWindow {
    static_assert(N > 0, "RollingWindow size must be positive");
    static_assert(N <= 10000, "RollingWindow size unreasonably large");
public:
    void push(T value);
    bool full() const;
    std::size_t size() const;
    T latest() const;
    T mean() const;
    T stddev() const;          // population stddev
    std::span<const T> raw_view() const;

private:
    std::array<T, N> buf_{};
    std::size_t head_{0};
    std::size_t count_{0};
};
```

Default window sizes as `constexpr` constants so they are usable as template arguments:

```cpp
inline constexpr std::size_t kMaFastWindow = 20;
inline constexpr std::size_t kMaSlowWindow = 100;
inline constexpr std::size_t kMomLookback  = 60;
inline constexpr std::size_t kMomSkip      = 5;
inline constexpr std::size_t kVolWindow    = 20;
inline constexpr double kAnnualizationFactor = 252.0;

// Usage — zero runtime cost, size known at compile time:
RollingWindow<double, kMaFastWindow>  fast_ma;
RollingWindow<double, kMaSlowWindow>  slow_ma;
RollingWindow<double, kVolWindow>     vol_est;
```

For ingestion-layer structs (`FuturesContract`, `RollEvent`, `MarketMacroObservation`, `ReleaseMacroObservation`), see `data_ingestion.md §6`.

---

## 5. Concurrency Model

### Option A: Parallelize by symbol (baseline)

Each task processes one symbol end-to-end independently.

- Pros: simple, clean, low synchronization burden, easy to debug
- Cons: less granular work balancing

### Option B: Parallelize by pipeline stage

One queue per stage (data → features → results).

- Pros: closer to a real streaming engine, better systems story
- Cons: more complex, harder to debug

**Start with Option A.** It demonstrates concurrency clearly while still being defensible.

### Experiment-level parallelism

The most valuable parallelism for parameter sweeps:

```text
main thread
    |
    +-- build experiment list
    +-- submit experiments to thread pool
    +-- collect results via std::future
```

Each worker thread executes one experiment independently with isolated local state.

### Parallel execution rules

Safe shared state (read-only):
- market data cache
- macro data cache
- immutable config templates

Must be synchronized:
- result containers
- log/file writers
- progress counters

Recommended pattern: worker receives immutable config → builds local model state → runs backtest → emits local result → pushes result into synchronized aggregator.

---

## 6. Design Principles

### 1. Correctness before speed
Validate correctness first. Add performance improvements second. Profile before optimizing.

### 2. RAII everywhere
Locks owned by `std::lock_guard` / `std::unique_lock`. Threads owned by classes that shut them down in destructors. Dynamic resources owned by `std::unique_ptr`. Cleanup in destructors, not fragile manual code paths.

### 3. Separate ownership from access
One object owns a resource. Others receive references, spans, or const views. Ask: who owns this? Who only reads it? Who may mutate it?

### 4. Concurrency explicit and contained
Isolate concurrency to a few infrastructure classes. Keep finance logic single-threaded and deterministic. Use the thread pool as the main concurrency boundary.

### 5. Templates reduce duplication
Good: `ThreadSafeQueue<T>`, `RollingWindow<T>`, generic numerical helpers. Bad: template metaprogramming for style, deeply nested generics that hurt readability. The professor should be able to see why the template exists.

### 6. Value semantics by default
Prefer stack values, vectors of values, return by value, move when needed. Use heap allocation only when lifetime must outlive scope, polymorphism is needed, or the object is too large for the stack.

### 7. Narrow module interfaces
Simple headers, few public methods, low coupling, no unnecessary shared mutable state.

### 8. Visible invariants

- **ThreadSafeQueue**: pushed values valid until popped; waiters wake correctly; closed queue unblocks waiters
- **ThreadPool**: tasks run at most once; worker threads stop cleanly; destructor does not leak threads
- **Backtester**: positions evolve consistently with signals; returns use aligned timestamps; metrics from realized equity curve

### 9. Extension without over-engineering
Build clear extension points (new signals, execution models, metrics, input formats). Do not build speculative plugin frameworks or unnecessary inheritance trees.

### 10. Composition over inheritance
`Backtester` uses a signal object. `ResultAggregator` uses metric functions. `ThreadPool` uses a task queue. Deep inheritance is the wrong default.

### 11. Make debugging easy
Deterministic small test datasets, clear logs, simple config, assertions on assumptions, modular tests.

### 12. Frame as a systems project
This is a systems-programming engine whose application domain is quantitative finance. Threads → parallel tasks. Queues → producer-consumer. Templates → reusable abstractions. RAII → correctness under failure. Backtesting → concrete use case.

### 13. Three lenses for every class

- **Systems**: Is ownership clear? Is lifetime safe? Is concurrency correct?
- **Design**: Is this module focused? Is the interface small? Can I explain it quickly?
- **Quant usefulness**: Would I actually use this later? Does it connect to portfolio or signal research?

### 14. Project mantra
*Build a small, correct, extensible engine that demonstrates modern C++ principles through a realistic quant workflow.*

---

## 7. Repository Structure

```text
cpp_project/
├── CMakeLists.txt
├── README.md
├── ARCHITECTURE.md
├── RESEARCH.md
├── data_ingestion.md
├── .env.example
├── data/
│   ├── raw/
│   │   ├── databento/          # gitignored — licensed source data
│   │   ├── fred/               # 9 FRED macro series CSVs
│   │   └── vix/                # CBOE VIX daily CSV
│   ├── processed/
│   │   ├── continuous/         # Panama-adjusted futures CSVs (9 symbols)
│   │   └── macro/              # macro_panel.csv — 20 z-score features × 4097 dates
│   └── sample/                 # 5-bar CSVs for unit tests
├── output/                     # experiment artifacts (gitignored)
│   └── <experiment_id>/
│       ├── <SYMBOL>_equity.csv
│       └── <SYMBOL>_positions.csv
├── scripts/
│   ├── download_data.py        # parallel Databento + FRED + VIX download
│   ├── build_continuous.py     # Panama back-adjustment
│   └── build_macro_panel.py    # z-score features from FRED + VIX
├── include/
│   ├── core/
│   │   ├── Constants.hpp       # constexpr baseline values + kISSplitDate
│   │   ├── DailyBar.hpp
│   │   ├── MarketEvent.hpp
│   │   ├── RollingWindow.hpp   # template circular buffer
│   │   ├── ThreadPool.hpp      # std::jthread-based thread pool
│   │   └── ThreadSafeQueue.hpp
│   ├── data/
│   │   ├── CSVLoader.hpp
│   │   ├── MarketDataLoader.hpp
│   │   └── MacroDataLoader.hpp # MacroPanel — date-indexed macro feature map
│   ├── signals/
│   │   ├── SignalResult.hpp    # SignalResult struct + SignalComputable concept
│   │   ├── MovingAverageSignal.hpp
│   │   ├── MomentumSignal.hpp
│   │   ├── VolatilitySignal.hpp
│   │   └── RegimeClassifier.hpp # 4-regime softmax classifier
│   └── backtest/
│       ├── PerformanceMetrics.hpp
│       ├── Backtester.hpp       # DailyPosition + RunResult + IS/OOS support
│       ├── ResultAggregator.hpp # portfolio_pnl() cross-symbol inv-vol
│       └── ExperimentConfig.hpp # typed config struct with constexpr defaults
├── src/
│   ├── main.cpp                 # CLI, IS/OOS reporting, artifact writing
│   ├── core/
│   │   └── ThreadPool.cpp
│   ├── data/
│   │   ├── CSVLoader.cpp
│   │   ├── MarketDataLoader.cpp
│   │   └── MacroDataLoader.cpp
│   ├── signals/
│   │   ├── MovingAverageSignal.cpp
│   │   ├── MomentumSignal.cpp
│   │   ├── VolatilitySignal.cpp
│   │   └── RegimeClassifier.cpp
│   └── backtest/
│       ├── Backtester.cpp
│       └── ResultAggregator.cpp
└── tests/
    ├── test_csv_loader.cpp
    ├── test_thread_safe_queue.cpp
    ├── test_thread_pool.cpp
    ├── test_signals.cpp
    ├── test_backtester.cpp
    └── test_result_aggregator.cpp
```

---

## 8. Implementation Order

Build in stages. Do not begin with the full architecture.

### Stage 1: Static data pipeline
Build `MarketEvent` and `DailyBar`. Build CSV loader. Parse one file. Print clean loaded records.

### Stage 2: Thread-safe queue
Build `ThreadSafeQueue<T>` with `push()`, `wait_and_pop()`, `try_pop()`, `close()`. Use `std::mutex`, `std::condition_variable`, RAII locks.

### Stage 3: Thread pool
Build `ThreadPool`. Worker threads wait for tasks. Tasks are `std::function<void()>`. Support graceful shutdown. Return results using `std::future`.

### Stage 4: Signal engine
Define a common signal interface or concept. Compute signals per symbol. Parallelize across symbols using the thread pool.

### Stage 5: Backtester
Convert signals into positions. Compute simple PnL. Support long/flat first, then long/short. Add transaction costs later.

### Stage 6: Result aggregation
Aggregate outputs. Produce summary metrics. Write results to stdout or CSV. Add experiment ID and config saving.

---

## 9. Technical Requirements

### Ownership
Prefer `std::unique_ptr`, stack allocation, and references where ownership does not transfer. Avoid raw `new` and `delete`.

### Concurrency
The queue and thread pool must be correct before they are fast. Priority: correctness → safety → clarity → performance.

### Templates
Use templates where they make the design genuinely more reusable (`ThreadSafeQueue<T>`, `RollingWindow<T, N>`, indicator utilities). Do not force templates everywhere.

### `constexpr` and `static_assert`
Window sizes and universe constants are `constexpr` so they can serve as template arguments and are evaluated at compile time with zero runtime cost. `static_assert` in `RollingWindow<T, N>` validates template parameters and gives clear error messages — use this pattern wherever template preconditions exist.

### Error handling
Use exceptions only where they help preserve invariants. Use RAII so partial failure does not leak resources. A failed experiment records `status = FAILED` with error message; the sweep continues.

---

## 10. Design Boundaries

These boundaries must be kept strict:

- Loader does not compute signals
- Thread pool does not know finance logic
- Signal engine does not own threads
- Backtester does not parse CSV
- Aggregator does not run simulations

---

## 11. Test Strategy

### Unit tests
- CSV / DBN parsing
- queue correctness (single-threaded)
- thread pool task execution
- moving average and momentum calculations
- backtest arithmetic (returns, drawdowns)

### Integration tests
- full pipeline on 1 symbol
- full pipeline on 3 symbols (parallel)
- shutdown behavior (graceful stop under load)

---

## 12. Data Ingestion Reference

The data ingestion layer — offline download scripts, Databento + FRED integration, continuous futures construction, Panama adjustment algorithm, local file layout, and struct definitions — is fully specified in `data_ingestion.md`.

The C++ engine reads only from `data/processed/` at runtime.

---

## 13. Future Extensions

- **Mean reversion signal**: z-score on returns, short-term reversal, rolling regression; feeds into regime-weighted combination alongside existing trend signals
- **MarketDataCache with `shared_mutex`**: after initial load, store bars in a shared read-only cache; workers hold `std::shared_lock` concurrently, loader holds `std::unique_lock` once during write — canonical reader-writer pattern
- **JSON config output**: serialize `ExperimentConfig` to `config.json` per experiment using `std::format` or a lightweight JSON library
- **`std::variant`-based config**: for YAML/JSON-driven parameter sweeps, `ConfigValue = std::variant<int, double, std::string, bool>` with `std::visit` dispatch
- **HMM regime model**: Hidden Markov Model over macro state; Baum-Welch forward pass for filtered regime probabilities
- **Carry signal**: roll-yield-based directional signal; deferred from baseline
- **Interfaces**: `IDataSource`, `IFeature`, `IModel`, `IPortfolioConstructor` for swappable implementations
- **Portfolio optimizer**: mean-variance or risk-parity extension
- **Columnar storage**: in-memory feature store for cache efficiency
- **Factor model layer**: cross-sectional ranking, PCA, ridge signal combination
- **Live adapters**: separate research-time simulation from live-trading abstractions

---

## 14. Stretch Goals

- multi-asset cross-sectional strategy support
- factor signals (value, carry, cross-sectional momentum)
- JSON config parser
- async file writing
- profiling and optimization pass
- regime labeling and regime-by-regime performance breakdown
- parameter grid search with comparison table
