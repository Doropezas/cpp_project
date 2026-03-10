#!/usr/bin/env python3
"""
Offline data downloader for the Quant Research Engine.

Downloads:
  - Futures OHLCV (Databento, GLBX.MDP3, ohlcv-1d) for the 9-symbol universe
  - Macro series (FRED REST API) for the macro feature set
  - VIX (CBOE, no key required)

All data is written to data/raw/ as CSV files.
The C++ engine reads these files at runtime — no API calls in the binary.

Usage:
    python3 scripts/download_data.py              # uses .env in project root
    python3 scripts/download_data.py --start 2000-01-01 --end 2024-12-31
    python3 scripts/download_data.py --symbols ES NQ GC   # subset

Keys are read from environment variables (never hardcoded):
    DATABENTO_API_KEY
    FRED_API_KEY

Load them with:
    source .env          # bash: export VAR=value lines
    # or set them in your shell before running this script
"""

import argparse
import csv
import io
import os
import sys
import time
from datetime import date, timedelta
from pathlib import Path

import requests

# Config

PROJECT_ROOT = Path(__file__).resolve().parent.parent
RAW_DIR = PROJECT_ROOT / "data" / "raw"

# 9-symbol baseline universe (RESEARCH.md §2)
# Maps symbol root → Databento continuous front-month instrument id pattern
FUTURES_SYMBOLS = {
    "ES": "ES.c.0",  # S&P 500 E-mini
    "NQ": "NQ.c.0",  # Nasdaq 100 E-mini
    "YM": "YM.c.0",  # Dow Jones E-mini
    "ZN": "ZN.c.0",  # 10-Year T-Note
    "ZB": "ZB.c.0",  # 30-Year T-Bond
    "GC": "GC.c.0",  # Gold
    "CL": "CL.c.0",  # Crude Oil (WTI)
    "6E": "6E.c.0",  # Euro FX
    "6J": "6J.c.0",  # Japanese Yen
}

# FRED series (RESEARCH.md §8)
FRED_SERIES = {
    "DGS2": "2-Year Treasury Yield",
    "DGS10": "10-Year Treasury Yield",
    "T10Y3M": "10Y-3M Treasury Spread",
    "T10YIE": "10-Year Breakeven Inflation",
    "DFII10": "10-Year TIPS Yield",
    "BAA10Y": "BAA-10Y Credit Spread",
    "DTWEXBGS": "USD Broad Trade-Weighted Index",
    "STLFSI4": "St. Louis Fed Stress Index",
    "WALCL": "Fed Balance Sheet (Total Assets)",
}

FRED_BASE = "https://fred.stlouisfed.org/graph/fredgraph.csv"

# VIX — CBOE direct download (no API key)
VIX_URL = "https://cdn.cboe.com/api/global/us_indices/daily_prices/VIX_History.csv"

DEFAULT_START = "2010-06-06"  # GLBX.MDP3 available from 2010-06-06
DEFAULT_START_MACRO = "2010-01-01"  # FRED/VIX available from earlier
DEFAULT_END = date.today().isoformat()


# Helpers


def require_env(var: str) -> str:
    """Read an env var; abort with a clear message if missing or empty."""
    val = os.environ.get(var, "").strip()
    if not val:
        sys.exit(
            f"ERROR: {var} is not set.\n"
            f"  Add it to .env and run:  source .env\n"
            f"  Or set it directly:      export {var}=your_key_here"
        )
    return val


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def fred_csv_url(series_id: str, api_key: str, start: str, end: str) -> str:
    return (
        f"https://fred.stlouisfed.org/graph/fredgraph.csv"
        f"?id={series_id}"
        f"&vintage_date={end}"  # point-in-time: data as known on `end`
    )


# Databento


def _download_one_symbol(
    sym: str, start: str, end: str, databento_key: str, out_dir: Path
) -> str:
    """Download one futures symbol. Returns a status string for printing."""
    import databento as db
    import warnings

    instrument = FUTURES_SYMBOLS[sym]
    out_path = out_dir / f"{sym}.csv"

    if out_path.exists():
        return f"  [skip] {sym} — already downloaded"

    try:
        client = db.Historical(
            databento_key
        )  # one client per thread (not thread-safe to share)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")  # suppress degraded-quality BentoWarnings
            data = client.timeseries.get_range(
                dataset="GLBX.MDP3",
                schema="ohlcv-1d",
                symbols=[instrument],
                stype_in="continuous",
                start=start,
                end=end,
            )
        df = data.to_df()

        with open(out_path, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(
                ["symbol", "date", "open", "high", "low", "close", "volume"]
            )
            for ts, row in df.iterrows():
                writer.writerow(
                    [
                        sym,
                        str(ts)[:10],
                        row["open"],
                        row["high"],
                        row["low"],
                        row["close"],
                        int(row["volume"]),
                    ]
                )
        return f"  OK  {sym:4s} — {len(df)} bars"
    except Exception as e:
        return f"  FAIL {sym:4s} — {e}"


def download_futures(
    symbols: list[str], start: str, end: str, databento_key: str
) -> None:
    from concurrent.futures import ThreadPoolExecutor, as_completed

    out_dir = RAW_DIR / "databento"
    ensure_dir(out_dir)

    print(f"  Fetching {len(symbols)} symbols in parallel ...")
    with ThreadPoolExecutor(max_workers=len(symbols)) as pool:
        futures = {
            pool.submit(
                _download_one_symbol, sym, start, end, databento_key, out_dir
            ): sym
            for sym in symbols
        }
        for fut in as_completed(futures):
            print(fut.result(), flush=True)


# FRED


def download_fred(start: str, end: str, fred_key: str) -> None:
    out_dir = RAW_DIR / "fred"
    ensure_dir(out_dir)

    fred_api_base = "https://api.stlouisfed.org/fred/series/observations"

    for series_id, description in FRED_SERIES.items():
        out_path = out_dir / f"{series_id}.csv"

        if out_path.exists():
            print(f"  [skip] {series_id} ({description}) — already downloaded")
            continue

        print(f"  Downloading {series_id} ({description}) ...", end=" ", flush=True)
        try:
            resp = requests.get(
                fred_api_base,
                params={
                    "series_id": series_id,
                    "observation_start": start,
                    "observation_end": end,
                    "api_key": fred_key,
                    "file_type": "json",
                },
                timeout=30,
            )
            resp.raise_for_status()
            observations = resp.json()["observations"]

            with open(out_path, "w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(["series_id", "date", "value"])
                count = 0
                for obs in observations:
                    if obs["value"] == ".":  # FRED uses "." for missing
                        continue
                    writer.writerow([series_id, obs["date"], obs["value"]])
                    count += 1
            print(f"OK ({count} observations)")
            time.sleep(0.1)  # be polite to FRED API
        except Exception as e:
            print(f"FAILED: {e}")


# VIX


def download_vix(start: str) -> None:
    out_dir = RAW_DIR / "vix"
    out_path = out_dir / "VIX.csv"
    ensure_dir(out_dir)

    if out_path.exists():
        print(f"  [skip] VIX — already downloaded at {out_path}")
        return

    print(f"  Downloading VIX (CBOE) ...", end=" ", flush=True)
    try:
        resp = requests.get(VIX_URL, timeout=30)
        resp.raise_for_status()

        # CBOE dates are MM/DD/YYYY — convert to YYYY-MM-DD for comparison
        def cboe_to_iso(d: str) -> str:
            m, day, y = d.split("/")
            return f"{y}-{m.zfill(2)}-{day.zfill(2)}"

        reader = csv.DictReader(io.StringIO(resp.text))
        rows = [
            (cboe_to_iso(r["DATE"]), r["CLOSE"])
            for r in reader
            if cboe_to_iso(r["DATE"]) >= start
        ]

        with open(out_path, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["series_id", "date", "value"])
            for iso_date, close in rows:
                writer.writerow(["VIX", iso_date, close])
        print(f"OK ({len(rows)} observations)")
    except Exception as e:
        print(f"FAILED: {e}")


# Entry Point


def main() -> None:
    parser = argparse.ArgumentParser(description="Download market and macro data")
    parser.add_argument(
        "--start", default=DEFAULT_START, help="Futures start date YYYY-MM-DD"
    )
    parser.add_argument(
        "--start-macro",
        default=DEFAULT_START_MACRO,
        help="Macro/VIX start date YYYY-MM-DD",
    )
    parser.add_argument("--end", default=DEFAULT_END, help="End date YYYY-MM-DD")
    parser.add_argument(
        "--symbols",
        nargs="+",
        default=list(FUTURES_SYMBOLS.keys()),
        choices=list(FUTURES_SYMBOLS.keys()),
        help="Subset of symbols to download",
    )
    parser.add_argument(
        "--skip-futures", action="store_true", help="Skip Databento download"
    )
    parser.add_argument("--skip-fred", action="store_true", help="Skip FRED download")
    parser.add_argument("--skip-vix", action="store_true", help="Skip VIX download")
    args = parser.parse_args()

    # Load .env if it exists (simple key=value parser, no shell required)
    env_file = PROJECT_ROOT / ".env"
    if env_file.exists():
        for line in env_file.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, _, v = line.partition("=")
                if v.strip():
                    os.environ.setdefault(k.strip(), v.strip())

    print(f"Futures range:    {args.start} → {args.end}")
    print(f"Macro/VIX range:  {args.start_macro} → {args.end}")
    print(f"Output dir: {RAW_DIR}\n")

    if not args.skip_futures:
        databento_key = require_env("DATABENTO_API_KEY")
        print("=== Futures (Databento) ===")
        download_futures(args.symbols, args.start, args.end, databento_key)
        print()

    if not args.skip_fred:
        fred_key = require_env("FRED_API_KEY")
        print("=== Macro (FRED) ===")
        download_fred(args.start_macro, args.end, fred_key)
        print()

    if not args.skip_vix:
        print("=== VIX (CBOE) ===")
        download_vix(args.start_macro)
        print()

    print("Done. Run the engine with:")
    print(f"  ./build/quant_engine data/processed/continuous")


if __name__ == "__main__":
    main()
