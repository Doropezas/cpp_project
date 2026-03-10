# Multithreaded Quant Research Engine

C++ research engine that runs trend-following and momentum signals across 9 macro futures in parallel, backtests them, and reports performance metrics.

Threads for parallel symbol processing, queues for the data pipeline, templates for reusable containers and indicators, RAII for correctness under failure.

## Quick Start

```bash
# Build
cmake -S . -B build && cmake --build build

# Run on included data (no download needed)
./build/quant_engine data/processed/continuous

# Run tests
ctest --test-dir build
```

## What it does

Loads daily OHLCV bars for 9 futures (ES, NQ, YM, ZN, ZB, GC, CL, 6E, 6J), computes three signals per symbol, sizes positions by inverse volatility, and runs the backtest in parallel — one thread per symbol. Prints Sharpe, max drawdown, hit ratio, and turnover per symbol plus a portfolio summary.

## Signals

| Signal | Logic |
|---|---|
| Moving Average | MA(20) vs MA(100) crossover → ±1 |
| Momentum | Price(t−5) / Price(t−60) → ±1 |
| Volatility | 20-day realized vol, annualized — used for position sizing |

## Modules

```
ThreadSafeQueue → ThreadPool → ResultAggregator
                                     ↑
CSVLoader → MarketDataLoader → Backtester ← SignalEngine
```

## C++20 use:

- `std::thread`, `std::mutex`, `std::condition_variable` (producer-consumer queue)
- `std::scoped_lock` on two mutexes (ResultAggregator)
- `std::future` + `std::packaged_task` (ThreadPool::submit)
- RAII: `JThread` auto-join wrapper, `unique_ptr`, `lock_guard`
- Templates: `ThreadSafeQueue<T>`, `RollingWindow<T, N>` with `static_assert`
- C++20 Concepts: `SignalComputable` constrains all signal classes
- `std::span`, `std::format`, `std::filesystem`, structured bindings
- Perfect forwarding, move semantics, rule of five

## Data

Included in the repo (`data/`):
- `processed/continuous/` — Panama back-adjusted daily bars, 2010–2026
- `raw/fred/` — 9 FRED macro series (yields, spreads, stress index)
- `raw/vix/` — CBOE VIX daily closes

## Docs

- [ARCHITECTURE.md](Architechture.md) — system design, concurrency model, module interfaces
- [RESEARCH.md](Research.md) — signals, portfolio construction, macro regime model
- [data_ingestion.md](data_ingestion.md) — data pipeline, Panama adjustment algorithm
