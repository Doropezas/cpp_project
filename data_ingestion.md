# data_ingestion.md
**Multithreaded Quant Research Engine**

## 1. Philosophy — Offline-First Ingestion

The research engine reads only local files at runtime — no network calls in the C++ binary.

All data is downloaded once via offline scripts and stored under `data/raw/`. The C++ engine only needs a path to `data/` and has zero knowledge of API keys.

Rationale:
- **Determinism**: the same local files produce the same results every run
- **Speed**: no latency from network calls during research
- **Reproducibility**: snapshots of data can be committed or archived
- **API key safety**: keys never appear in C++ source or compiled binaries
- **Separation of concerns**: ingestion logic is Python/shell; research logic is C++

---

## 2. Databento Integration

**Source:** Databento historical API, schema `ohlcv-1d`, dataset `GLBX.MDP3`

**Auth:** `DATABENTO_API_KEY` environment variable — never hardcode or commit.

**What to download:**
- Front-month contract bars for each symbol
- One deferred contract per symbol (for roll detection)

**Raw format:** DBN files written to `data/raw/databento/`, one file per symbol/contract.

**At runtime:** the C++ engine reads DBN files locally. No live API calls.

---

## 3. FRED / ALFRED Integration

**Source:** FRED REST API (`fred/series/observations`)

**Auth:** `FRED_API_KEY` environment variable.

**Download target:** `data/raw/fred/`, one CSV or JSON file per series.

**Series list:**

| Series | Description |
|--------|-------------|
| DGS2 | 2-year Treasury yield |
| DGS10 | 10-year Treasury yield |
| T10Y3M | 10Y−3M yield spread (recession proxy) |
| T10YIE | 10-year breakeven inflation |
| DFII10 | 10-year TIPS real yield |
| BAA10Y | Baa corporate spread over 10Y Treasury |
| DTWEXBGS | Nominal broad U.S. dollar index |
| STLFSI4 | St. Louis Fed Financial Stress Index |
| WALCL | Federal Reserve total assets (balance sheet) |

**VIX:** sourced separately from CBOE, stored in `data/raw/vix/`.

**Point-in-time classification:**

| Series | Type | Safe for backtest |
|--------|------|-------------------|
| DGS2, DGS10, T10YIE, DFII10, BAA10Y, VIX, DTWEXBGS | Market-based | Yes — no revisions |
| STLFSI4 | Constructed index | Mostly yes — small publication lag |
| WALCL | Weekly release | Track `available_from` date |

For STLFSI4 and WALCL, the download script records the `available_from` date alongside the observation date so the engine can avoid look-ahead bias.

---

## 4. Local File Layout

```
data/
├── raw/
│   ├── databento/          # DBN files per symbol/contract
│   ├── fred/               # one CSV/JSON per series
│   └── vix/                # CBOE VIX historical data
├── processed/
│   ├── continuous/         # Panama-adjusted series per symbol
│   ├── macro/              # joined daily macro state vector
│   └── features/           # model-ready feature panels
└── manifests/
    └── data_manifest.json  # checksums, download timestamps, series versions
```

---

## 5. Continuous Futures Construction

### Problem

Raw futures contracts expire. Price gaps at roll dates corrupt return series — a large apparent price move that is purely a contract switch, not a real return. All signals must be computed on a smooth, gap-free series.

### Method: Panama Additive Back-Adjustment

Back-adjust all historical prices by the cumulative sum of roll gaps, so that returns computed anywhere on the series are correct.

### Roll Trigger

**Primary:** volume crossover — roll when deferred contract volume exceeds front-month volume.

**Fallback:** 5 business days before front-month expiry.

### Algorithm (step-by-step)

1. Load all individual contract bars for a symbol from `data/raw/databento/`.
2. Build a `RollSchedule`: an ordered list of `(roll_date, old_contract, new_contract)` events.
3. For each roll event: compute the price gap as `gap = P_new(roll_date) − P_old(roll_date)`.
4. Subtract the gap from all prior adjusted prices (additive back-adjustment propagates backward).
5. Stitch all contracts into one continuous adjusted series.
6. Write adjusted series to `data/processed/continuous/<symbol>.csv`.

### Return Computation Rule

**Always** compute returns as `log(P_adj[t] / P_adj[t-1])` on the adjusted series.

**Never** compute returns by crossing a roll boundary on the unadjusted series — this would include the roll gap as a phantom return.

---

## 6. Key Data Structures

```cpp
struct FuturesContract {
    std::string symbol;
    std::string contract_id;
    std::string exchange;
    std::string last_trade_date;
    int expiry_year;
    int expiry_month;
};

struct RollEvent {
    std::string symbol;
    std::string roll_date;
    std::string old_contract;
    std::string new_contract;
    double price_gap;       // P_new - P_old on roll_date
    std::string roll_trigger; // "volume_crossover" or "expiry_fallback"
};

struct DailyBar {
    std::string symbol;
    std::string date;
    double open{};
    double high{};
    double low{};
    double close{};          // Panama-adjusted close
    double close_unadjusted{};
    double volume{};
    double return_1d{};      // log(close[t] / close[t-1]) on adjusted series
    bool is_roll_date{false};
};

struct MarketMacroObservation {
    std::string series_id;
    std::string date;
    double value{};
};

struct ReleaseMacroObservation {
    std::string series_id;
    std::string observation_date;
    std::string available_from; // date when value was actually published
    double value{};
};
```

---

## 7. Data Pipeline Diagram

```text
  OFFLINE DOWNLOAD SCRIPTS
  (Python / shell — run once)
  ┌─────────────────────────────────────────────────────┐
  │                                                     │
  │  scripts/download_data.sh (or .py)                  │
  │  reads: DATABENTO_API_KEY, FRED_API_KEY (env vars)  │
  │                                                     │
  │   Databento API ──────────────────► data/raw/databento/
  │   FRED REST API ──────────────────► data/raw/fred/
  │   CBOE VIX ───────────────────────► data/raw/vix/
  │                                                     │
  └─────────────────────────────────────────────────────┘
                         │
                         ▼
  OFFLINE PREPROCESSING SCRIPTS
  ┌─────────────────────────────────────────────────────┐
  │                                                     │
  │  build_continuous.py                                │
  │    reads raw DBN files                              │
  │    applies Panama back-adjustment                   │
  │    writes ──────────────────────► data/processed/continuous/
  │                                                     │
  │  build_macro_panel.py                               │
  │    joins FRED + VIX, aligns calendar                │
  │    writes ──────────────────────► data/processed/macro/
  │                                                     │
  └─────────────────────────────────────────────────────┘
                         │
                         ▼
  C++ RESEARCH ENGINE (runtime — local reads only)
  ┌─────────────────────────────────────────────────────┐
  │                                                     │
  │  MarketDataLoader                                   │
  │    reads data/processed/continuous/                 │
  │    reads data/processed/macro/                      │
  │    produces vector<DailyBar>                        │
  │                                                     │
  │  → Feature Engineering → Model Layer                │
  │  → Portfolio Construction → Backtester              │
  │  → Result Aggregator → metrics output               │
  │                                                     │
  │  Zero network calls. Zero API key awareness.        │
  │                                                     │
  └─────────────────────────────────────────────────────┘
```

---

## 8. Auth Handling

- Both API keys are read from environment variables only (`DATABENTO_API_KEY`, `FRED_API_KEY`).
- `.gitignore` covers `.env` files and any credential files.
- The offline download script reads keys, fetches data, and writes local files.
- The C++ binary only receives a path to `data/` — it has no knowledge of or access to API keys.
- Do not hardcode keys anywhere in source code, config files, or committed YAML.
