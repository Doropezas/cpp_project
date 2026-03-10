#pragma once

#include <cstddef>
#include <string_view>

// Baseline signal and portfolio parameters from RESEARCH.md §13.
//
// These are constexpr so they can be used as template arguments
// (e.g., RollingWindow<double, kVolWindow>) with zero runtime cost.
// Runtime-configurable overrides are accepted by each class constructor;
// these constants serve as the default values.

inline constexpr std::size_t kMaFastWindow = 20;   // MA crossover fast window
inline constexpr std::size_t kMaSlowWindow = 100;  // MA crossover slow window
inline constexpr std::size_t kMomLookback  = 60;   // momentum lookback
inline constexpr std::size_t kMomSkip      = 5;    // momentum skip (reversal guard)
inline constexpr std::size_t kVolWindow    = 20;   // realized volatility window

inline constexpr double kAnnualizationFactor = 252.0;  // trading days per year

// IS / OOS date split (RESEARCH.md §4): first 70% in-sample, last 30% out-of-sample.
// Approximate split: 2010-06-07 → 2021-12-31 IS, 2022-01-01 → present OOS.
inline constexpr std::string_view kISSplitDate = "2021-12-31";
