# Multithreaded Quant Research Engine

A modern C++ project that demonstrates concurrency, RAII, templates, and modular design through a realistic quantitative research workflow using daily macro futures.

## Modules

- **Market Data Loader** — reads local processed files, produces typed `DailyBar` objects
- **Thread Pool** — task submission layer with `std::future`-based result collection
- **Signal Engine** — computes trend, momentum, and volatility signals per symbol
- **Backtester** — converts signals into positions, computes PnL and equity curve
- **Result Aggregator** — collects parallel results, computes portfolio-level metrics

## Why This Project

This is a **systems-programming engine whose application domain is quantitative finance**. Every technical decision is justified by the domain:

- threads because per-symbol and per-experiment tasks are naturally parallel
- queues because producer-consumer is realistic for a data pipeline
- templates because indicators and containers are genuinely reusable
- RAII because correctness under failure matters in financial systems
- backtesting because it gives a concrete, measurable use case

## Course Concepts Demonstrated

- object lifetimes and smart pointers (`std::unique_ptr`, RAII)
- move semantics and value semantics
- `std::thread`, `std::mutex`, `std::condition_variable`
- `std::future` / async-style task results
- templates (`ThreadSafeQueue<T>`, `RollingWindow<T>`)
- modular architecture with narrow interfaces

## Quick Start

```bash
# 1. Download data (offline — run once)
python scripts/download_data.py

# 2. Build
cmake -B build && cmake --build build

# 3. Run baseline experiment
./build/quant_engine --config configs/baseline.yaml
```

## Documentation

| File | Contents |
|------|----------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | System design, modules, concurrency, repo structure, test strategy |
| [RESEARCH.md](RESEARCH.md) | Signals, portfolio construction, macro features, regime model, experiments |
| [data_ingestion.md](data_ingestion.md) | Databento + FRED integration, continuous futures construction, data layout |
