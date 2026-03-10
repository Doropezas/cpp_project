#pragma once

#include "core/Constants.hpp"

#include <string>

// ExperimentConfig
//
// All parameters that define one backtest run (RESEARCH.md §12).
//
// Constructed from command-line arguments in main.cpp.
// Each field has a documented default matching the baseline from RESEARCH.md §13.
//
// Field descriptions:
//   id          — unique identifier used for output file naming
//   data_path   — directory containing processed continuous-series CSVs
//   macro_path  — path to the macro panel CSV (empty = skip macro/regime)
//   output_dir  — where to write experiment artifacts (equity_curve.csv, positions.csv)
//   split_date  — IS/OOS boundary; dates ≤ split_date are in-sample
//   ma_fast     — MA crossover fast window
//   ma_slow     — MA crossover slow window
//   mom_lb      — momentum lookback
//   mom_skip    — momentum skip (reversal guard)
//   use_regime  — if true and macro_path is non-empty, applies regime scaling

struct ExperimentConfig
{
    std::string id         {"baseline"};
    std::string data_path  {"data/processed/continuous"};
    std::string macro_path {"data/processed/macro/macro_panel.csv"};
    std::string output_dir {"output"};
    std::string split_date {std::string(kISSplitDate)};

    std::size_t ma_fast  {kMaFastWindow};
    std::size_t ma_slow  {kMaSlowWindow};
    std::size_t mom_lb   {kMomLookback};
    std::size_t mom_skip {kMomSkip};

    bool use_regime{false};
};
