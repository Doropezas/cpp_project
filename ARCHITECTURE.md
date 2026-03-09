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

### 3.2 Thread Pool

Owns worker threads, accepts tasks, returns futures to callers, shuts down gracefully.

```cpp
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads);
    ~ThreadPool();

    template<class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

private:
    // task queue, mutex, condition_variable, shutdown flag, worker loop
};
```

The thread pool does not know anything about finance logic.

### 3.3 Signal Engine

Computes reusable indicators and transforms them into trading signals. Does not own threads.

```cpp
struct SignalResult {
    std::string symbol;
    std::vector<double> signal_values;
};

class MovingAverageSignal { /* ... */ };
class MomentumSignal      { /* ... */ };
class VolatilitySignal    { /* ... */ };
```

Signal parameters (windows, thresholds) are passed in via config — not hardcoded.

### 3.4 Backtester

Converts signal outputs into positions, applies position rules, computes returns, tracks portfolio evolution.

- signal > 0 → long; signal < 0 → short; signal = 0 → flat
- daily rebalance by default
- later extensions: transaction costs, slippage, leverage, cash accounting

### 3.5 Result Aggregator

Combines results from many parallel jobs, computes summary statistics, produces stable output format.

Metrics: total return, annualized return, volatility, Sharpe ratio, max drawdown, win rate, turnover.

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
    double cumulative_return{};
    double annualized_return{};
    double annualized_volatility{};
    double sharpe_ratio{};
    double max_drawdown{};
    double hit_ratio{};
    double turnover{};
};

struct BacktestResult {
    std::string symbol;
    std::vector<double> returns;
    std::vector<double> equity_curve;
    PerformanceMetrics metrics;
};
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
quant-research-engine/
├── CMakeLists.txt
├── README.md
├── ARCHITECTURE.md
├── RESEARCH.md
├── data_ingestion.md
├── configs/
│   └── baseline.yaml
├── data/
│   ├── raw/
│   │   ├── databento/
│   │   ├── fred/
│   │   └── vix/
│   ├── processed/
│   │   ├── continuous/
│   │   ├── macro/
│   │   └── features/
│   └── manifests/
│       └── data_manifest.json
├── scripts/
│   ├── download_data.py
│   ├── build_continuous.py
│   └── build_macro_panel.py
├── include/
│   ├── core/
│   │   ├── MarketEvent.hpp
│   │   ├── DailyBar.hpp
│   │   ├── ThreadSafeQueue.hpp
│   │   ├── ThreadPool.hpp
│   │   ├── ResultTypes.hpp
│   │   └── Config.hpp
│   ├── data/
│   │   ├── CSVLoader.hpp
│   │   └── MarketDataLoader.hpp
│   ├── signals/
│   │   ├── Signal.hpp
│   │   ├── MovingAverage.hpp
│   │   ├── Momentum.hpp
│   │   └── Volatility.hpp
│   ├── backtest/
│   │   ├── PortfolioState.hpp
│   │   ├── ExecutionModel.hpp
│   │   └── Backtester.hpp
│   └── analytics/
│       ├── Metrics.hpp
│       └── ResultAggregator.hpp
├── src/
│   ├── main.cpp
│   ├── data/
│   ├── signals/
│   ├── backtest/
│   └── analytics/
├── results/
│   └── experiments/
│       └── <experiment_id>/
│           ├── config.json
│           ├── metrics.json
│           ├── equity_curve.csv
│           ├── positions.csv
│           └── signals.csv
└── tests/
    ├── test_queue.cpp
    ├── test_thread_pool.cpp
    ├── test_signals.cpp
    └── test_backtester.cpp
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
Use templates where they make the design genuinely more reusable (`ThreadSafeQueue<T>`, `RollingWindow<T>`, indicator utilities). Do not force templates everywhere.

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

After the MVP:

- **Interfaces**: `IDataSource`, `IFeature`, `IModel`, `IPortfolioConstructor` for swappable implementations
- **Regime engine**: hidden Markov model over macro state vector
- **Portfolio optimizer**: mean-variance or risk-parity extension
- **Live adapters**: separate research-time simulation from live-trading abstractions
- **Columnar storage**: in-memory feature store (columnar layout vs. row-based structs) for cache efficiency
- **Caching**: processed feature panels cached between runs
- **Factor model layer**: cross-sectional ranking, PCA, ridge signal combination
- **Benchmark comparison**: equal-weight buy-and-hold as baseline

---

## 14. Stretch Goals

- multi-asset cross-sectional strategy support
- factor signals (value, carry, cross-sectional momentum)
- JSON config parser
- async file writing
- profiling and optimization pass
- regime labeling and regime-by-regime performance breakdown
- parameter grid search with comparison table
