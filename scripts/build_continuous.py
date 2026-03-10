#!/usr/bin/env python3
"""
Panama additive back-adjustment for continuous futures series.
(data_ingestion.md §5)

Reads:  data/raw/databento/<SYM>.csv   (front-month continuous, unadjusted)
Writes: data/processed/continuous/<SYM>.csv

Algorithm
---------
Because we download ES.c.0 (Databento's own continuous series), the roll
stitching is already handled by Databento. However, Databento stitches by
price-ratio (multiplicative), whereas our design spec uses Panama additive
back-adjustment so that log-returns are never computed across a roll boundary
on the raw price.

We detect each roll date by finding days where consecutive bars belong to
different underlying contracts (identified by large overnight gaps relative to
recent volatility, a reliable proxy when the raw contract data is unavailable).
For each roll we compute the additive gap and subtract it from all prior bars,
building a single gap-adjusted series.

Output CSV columns:
    symbol, date, open, high, low, close, close_unadjusted, volume, is_roll_date

The C++ CSVLoader already reads this format (DailyBar struct fields).

Usage:
    python3 scripts/build_continuous.py
    python3 scripts/build_continuous.py --symbols ES GC
    python3 scripts/build_continuous.py --roll-threshold 3.0   # sigma multiplier
"""

import argparse
import csv
import math
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
RAW_DIR = PROJECT_ROOT / "data" / "raw" / "databento"
OUT_DIR = PROJECT_ROOT / "data" / "processed" / "continuous"

SYMBOLS = ["ES", "NQ", "YM", "ZN", "ZB", "GC", "CL", "6E", "6J"]

# is classified as a roll event (price discontinuity, not a real move).
DEFAULT_ROLL_SIGMA = 4.0


# Data Loading


def load_raw(sym: str) -> list[dict]:
    """Load raw CSV into list of dicts, sorted by date."""
    path = RAW_DIR / f"{sym}.csv"
    if not path.exists():
        raise FileNotFoundError(
            f"Raw data not found: {path}\n"
            f"Run:  python3 scripts/download_data.py --symbols {sym}"
        )
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    rows.sort(key=lambda r: r["date"])
    return rows


# Rol detection


def detect_rolls(rows: list[dict], sigma_threshold: float) -> list[int]:
    """
    Return indices where a roll gap is detected.

    Strategy: compute the rolling 20-day stddev of log-returns. On each day,
    if |log(close[t] / close[t-1])| > sigma_threshold * rolling_std, flag it
    as a roll date. The first 20 bars (warmup) are never flagged.
    """
    closes = [float(r["close"]) for r in rows]
    n = len(closes)
    window = 20
    roll_indices: list[int] = []

    log_rets = [0.0] * n
    for i in range(1, n):
        if closes[i - 1] > 0 and closes[i] > 0:
            log_rets[i] = math.log(closes[i] / closes[i - 1])

    for i in range(window, n):
        recent = log_rets[i - window : i]
        mean = sum(recent) / window
        var = sum((x - mean) ** 2 for x in recent) / window
        std = math.sqrt(var) if var > 0 else 0.0

        if std > 0 and abs(log_rets[i]) > sigma_threshold * std:
            roll_indices.append(i)

    return roll_indices


# Panama adjustment


def panama_adjust(rows: list[dict], roll_indices: list[int]) -> list[dict]:
    """
    Apply additive Panama back-adjustment.

    For each roll at index i:
      gap = close[i] - close[i-1]   (price jump on roll day)
      Subtract gap from close (and open/high/low) for all bars before i.

    Also computes log return_1d on the adjusted series.
    """
    # Work with float copies of price columns
    opens = [float(r["open"]) for r in rows]
    highs = [float(r["high"]) for r in rows]
    lows = [float(r["low"]) for r in rows]
    closes = [float(r["close"]) for r in rows]
    raw_cls = closes[:]  # keep unadjusted copy

    is_roll = [False] * len(rows)
    for idx in roll_indices:
        is_roll[idx] = True

    # Apply gaps from latest roll back to earliest (order matters)
    for idx in sorted(roll_indices, reverse=True):
        gap = closes[idx] - closes[idx - 1]
        for j in range(idx):
            opens[j] -= gap
            highs[j] -= gap
            lows[j] -= gap
            closes[j] -= gap

    # Build output records
    result = []
    for i, r in enumerate(rows):
        ret_1d = 0.0
        if i > 0 and closes[i - 1] > 0 and closes[i] > 0:
            ret_1d = math.log(closes[i] / closes[i - 1])

        result.append(
            {
                "symbol": r["symbol"],
                "date": r["date"],
                "open": round(opens[i], 4),
                "high": round(highs[i], 4),
                "low": round(lows[i], 4),
                "close": round(closes[i], 4),
                "close_unadjusted": round(raw_cls[i], 4),
                "volume": r["volume"],
                "is_roll_date": "1" if is_roll[i] else "0",
            }
        )
    return result


# ── Output ────────────────────────────────────────────────────────────────────

FIELDNAMES = [
    "symbol",
    "date",
    "open",
    "high",
    "low",
    "close",
    "close_unadjusted",
    "volume",
    "is_roll_date",
]


def write_continuous(sym: str, rows: list[dict]) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_path = OUT_DIR / f"{sym}.csv"
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
        writer.writeheader()
        writer.writerows(rows)


# ── Per-symbol pipeline ───────────────────────────────────────────────────────


def build_symbol(sym: str, sigma_threshold: float) -> str:
    try:
        rows = load_raw(sym)
        roll_idx = detect_rolls(rows, sigma_threshold)
        adjusted = panama_adjust(rows, roll_idx)
        write_continuous(sym, adjusted)
        return (
            f"  OK  {sym:4s} — {len(adjusted)} bars, {len(roll_idx)} roll(s) detected"
        )
    except FileNotFoundError as e:
        return f"  SKIP {sym:4s} — {e}"
    except Exception as e:
        return f"  FAIL {sym:4s} — {e}"


# ── Entry point ───────────────────────────────────────────────────────────────


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Panama back-adjust continuous futures"
    )
    parser.add_argument(
        "--symbols",
        nargs="+",
        default=SYMBOLS,
        choices=SYMBOLS,
        help="Symbols to process",
    )
    parser.add_argument(
        "--roll-threshold",
        type=float,
        default=DEFAULT_ROLL_SIGMA,
        metavar="SIGMA",
        help="Gap size in rolling-stddev units to flag as a roll (default 4.0)",
    )
    args = parser.parse_args()

    print(f"Panama back-adjustment  (roll threshold: {args.roll_threshold}σ)")
    print(f"  Input:  {RAW_DIR}")
    print(f"  Output: {OUT_DIR}\n")

    for sym in args.symbols:
        print(build_symbol(sym, args.roll_threshold), flush=True)

    print(f"\nDone. Run the engine with:")
    print(f"  ./build/quant_engine {OUT_DIR}")


if __name__ == "__main__":
    main()
