#!/usr/bin/env python3
"""
build_macro_panel.py — Build the daily macro feature panel.

Reads:
  data/raw/fred/{DGS2,DGS10,T10Y3M,T10YIE,DFII10,BAA10Y,DTWEXBGS,STLFSI4,WALCL}.csv
  data/raw/vix/VIX.csv

Outputs:
  data/processed/macro/macro_panel.csv

Feature columns (RESEARCH.md §8):
  {SERIES}_z     — rolling 252-day z-score of the level
  {SERIES}_d20_z — rolling 252-day z-score of the 20-day difference

FRED CSVs have columns: series_id, date, value
VIX CSV has columns:    series_id, date, value

Note on point-in-time bias:
  Market-based series (DGS*, T10YIE, BAA10Y, VIX, DTWEXBGS) are published
  same-day — safe for backtesting.
  STLFSI4 and WALCL have publication lags (weekly). Using observation date
  for simplicity; flag if model is sensitive to this lag.
"""

import os
import sys
import pandas as pd
import numpy as np

FRED_DIR = "data/raw/fred"
VIX_FILE = "data/raw/vix/VIX.csv"
OUT_FILE = "data/processed/macro/macro_panel.csv"

FRED_SERIES = [
    "DGS2", "DGS10", "T10Y3M", "T10YIE",
    "DFII10", "BAA10Y", "DTWEXBGS", "STLFSI4", "WALCL",
]

Z_WINDOW  = 252   # rolling z-score window (trading days)
D20_LAG   = 20    # Δ20: 20-day difference


def load_series(filepath: str, series_id: str) -> pd.Series:
    """Load a single CSV as a pd.Series indexed by date."""
    df = pd.read_csv(filepath, parse_dates=["date"])
    df = df[df["value"].notna()].copy()
    # FRED uses "." for missing values
    df = df[df["value"].astype(str) != "."]
    df["value"] = pd.to_numeric(df["value"], errors="coerce")
    df = df.dropna(subset=["value"])
    df = df.set_index("date")["value"]
    df.name = series_id
    return df


def build_panel() -> pd.DataFrame:
    series_dict: dict[str, pd.Series] = {}

    # Load FRED series
    for sid in FRED_SERIES:
        path = os.path.join(FRED_DIR, f"{sid}.csv")
        if not os.path.exists(path):
            print(f"  Warning: {path} not found — skipping {sid}")
            continue
        try:
            s = load_series(path, sid)
            series_dict[sid] = s
            print(f"  Loaded {sid}: {len(s)} observations "
                  f"({s.index.min().date()} → {s.index.max().date()})")
        except Exception as e:
            print(f"  Error loading {sid}: {e}")

    # Load VIX
    if os.path.exists(VIX_FILE):
        try:
            s = load_series(VIX_FILE, "VIX")
            series_dict["VIX"] = s
            print(f"  Loaded VIX: {len(s)} observations "
                  f"({s.index.min().date()} → {s.index.max().date()})")
        except Exception as e:
            print(f"  Error loading VIX: {e}")
    else:
        print(f"  Warning: {VIX_FILE} not found — skipping VIX")

    if not series_dict:
        print("No series loaded. Check data/raw/fred/ and data/raw/vix/.")
        sys.exit(1)

    # Combine into a single DataFrame on a daily calendar.
    df = pd.DataFrame(series_dict)

    # Build a daily business-day index spanning all series.
    start = df.index.min()
    end   = df.index.max()
    daily_idx = pd.date_range(start=start, end=end, freq="B")

    df = df.reindex(daily_idx)

    # Forward-fill missing values (weekends, holidays, weekly releases).
    # Limit to 7 trading days to avoid stale data propagating too far.
    df = df.ffill(limit=7)

    print(f"\n  Panel shape after daily reindex + ffill: {df.shape}")

    # ── Compute features ─────────────────────────────────────────────────────
    feature_frames = []

    for col in df.columns:
        s = df[col]

        # Level z-score: (x - rolling_mean) / rolling_std
        roll_mean = s.rolling(Z_WINDOW, min_periods=Z_WINDOW // 2).mean()
        roll_std  = s.rolling(Z_WINDOW, min_periods=Z_WINDOW // 2).std()
        z = (s - roll_mean) / roll_std.replace(0, np.nan)
        z.name = f"{col}_z"
        feature_frames.append(z)

        # Δ20 z-score: z-score of the 20-day difference
        d20 = s.diff(D20_LAG)
        d20_mean = d20.rolling(Z_WINDOW, min_periods=Z_WINDOW // 2).mean()
        d20_std  = d20.rolling(Z_WINDOW, min_periods=Z_WINDOW // 2).std()
        d20_z = (d20 - d20_mean) / d20_std.replace(0, np.nan)
        d20_z.name = f"{col}_d20_z"
        feature_frames.append(d20_z)

    features = pd.concat(feature_frames, axis=1)
    features.index.name = "date"
    features.index = features.index.strftime("%Y-%m-%d")

    # Drop rows where all features are NaN (early warmup period).
    features = features.dropna(how="all")

    # Replace remaining NaN with 0.0 (neutral z-score for missing series).
    features = features.fillna(0.0)

    return features


def main():
    print("Building macro panel ...")
    os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)

    panel = build_panel()

    print(f"\n  Writing {len(panel)} rows × {len(panel.columns)} features "
          f"to {OUT_FILE}")
    panel.to_csv(OUT_FILE)
    print("Done.")


if __name__ == "__main__":
    main()
