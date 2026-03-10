#pragma once

#include <cstddef>

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
