#pragma once

#include "data/MacroDataLoader.hpp"

#include <array>
#include <string>
#include <unordered_map>

// RegimeClassifier
//
// Classifies the current macro environment into one of four regimes
// (RESEARCH.md §9) and returns the probability of each regime via softmax.
//
// Regimes:
//   0  Risk-On       — equity bull, tight spreads, low volatility
//   1  Risk-Off      — equity bear, wide spreads, high volatility
//   2  Inflationary  — rising breakevens, real yields, commodity outperformance
//   3  Disinflationary — falling inflation, falling yields, bond outperformance
//
// Score formula (RESEARCH.md §9.1):
//   S_k = a_k + w_k^T Z_t
//   p_k = exp(S_k) / Σ_j exp(S_j)   (softmax, numerically stable)
//
// Signal scalar:
//   Each regime has a multiplier for the trend-following signal.
//   Regime-weighted scalar = Σ_k p_k * kRegimeSignalScales[k]
//   Applied as: final_position = clamp(direction * scalar, -1, +1)
//
// Initial weights encode qualitative priors. These are meant to be adjusted
// by the researcher through parameter sweeps (RESEARCH.md §12).

struct RegimeProbabilities
{
    std::array<double, 4> probs{0.25, 0.25, 0.25, 0.25};

    // Index of the dominant (highest probability) regime.
    [[nodiscard]] int dominant() const;
};

class RegimeClassifier
{
public:
    // Default constructor loads the built-in prior weights.
    RegimeClassifier();

    // Classify the macro state Z_t and return regime probabilities.
    [[nodiscard]] RegimeProbabilities classify(const MacroFeatures &z) const;

    // Compute the regime-weighted signal scalar from probabilities.
    // Returns a value in roughly [-1, +1] that scales the trend-following
    // direction signal before position clamping.
    [[nodiscard]] double signal_scalar(const MacroFeatures &z) const;

    // Per-regime signal multipliers for the trend-following strategy:
    //   Risk-On (+1.0): amplify trend signals — equities trending up
    //   Risk-Off (-0.3): dampen/reverse — drawdowns dominate trend signals
    //   Inflationary (+0.7): trend works in commodities and bonds
    //   Disinflationary (+0.8): moderate — trend works, lower conviction
    static constexpr std::array<double, 4> kRegimeSignalScales = {1.0, -0.3, 0.7, 0.8};

private:
    struct RegimeWeights
    {
        double intercept{0.0};
        std::unordered_map<std::string, double> feature_weights;
    };

    std::array<RegimeWeights, 4> weights_;

    // Numerically stable softmax: subtract max before exponentiation.
    static std::array<double, 4> softmax(const std::array<double, 4> &scores);
};
