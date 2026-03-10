#include "signals/RegimeClassifier.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

// ── Default prior weights (RESEARCH.md §9) ───────────────────────────────────
//
// These encode qualitative macro priors:
//
//   Risk-On  (k=0): low VIX, low credit spreads, positive equity momentum
//   Risk-Off (k=1): high VIX, wide BAA spreads
//   Inflationary (k=2): high breakeven inflation (T10YIE), high real yields (DFII10)
//   Disinflationary (k=3): falling yields (negative DGS10 delta), negative breakevens
//
// Feature naming: {SERIES_ID}_z (z-score of level), {SERIES_ID}_d20_z (z-score of Δ20).
// All weights are deliberately small; the prior is nearly flat (uniform 25% each).
// The researcher adjusts weights through parameter sweeps (RESEARCH.md §12).

RegimeClassifier::RegimeClassifier()
{
    // k=0: Risk-On
    weights_[0].intercept = 0.0;
    weights_[0].feature_weights = {
        {"VIX_z",       -0.5},   // low VIX → risk-on
        {"BAA10Y_z",    -0.4},   // tight spreads → risk-on
        {"DGS10_z",      0.2},   // slightly higher yields → growth expectations
        {"T10Y3M_z",     0.3},   // steep curve → risk-on
    };

    // k=1: Risk-Off
    weights_[1].intercept = 0.0;
    weights_[1].feature_weights = {
        {"VIX_z",        0.6},   // high VIX → risk-off
        {"BAA10Y_z",     0.5},   // wide spreads → risk-off
        {"STLFSI4_z",    0.4},   // financial stress → risk-off
        {"T10Y3M_z",    -0.3},   // flat / inverted curve → recession risk
    };

    // k=2: Inflationary
    weights_[2].intercept = 0.0;
    weights_[2].feature_weights = {
        {"T10YIE_z",     0.6},   // high breakeven inflation
        {"DFII10_z",     0.4},   // rising real yields (later-cycle inflation)
        {"DGS10_d20_z",  0.3},   // yields rising quickly
        {"DTWEXBGS_z",  -0.2},   // weak USD often accompanies inflation
    };

    // k=3: Disinflationary
    weights_[3].intercept = 0.0;
    weights_[3].feature_weights = {
        {"T10YIE_z",    -0.5},   // falling breakeven inflation
        {"DGS10_z",     -0.4},   // falling yields
        {"DGS10_d20_z", -0.3},   // yields falling quickly
        {"DTWEXBGS_z",   0.3},   // strong USD accompanies disinflation
    };
}

// ── softmax ───────────────────────────────────────────────────────────────────

std::array<double, 4> RegimeClassifier::softmax(const std::array<double, 4> &scores)
{
    const double max_s = *std::max_element(scores.begin(), scores.end());

    std::array<double, 4> exp_s{};
    double sum = 0.0;
    for (int k = 0; k < 4; ++k) {
        exp_s[k] = std::exp(scores[k] - max_s);  // subtract max for numerical stability
        sum += exp_s[k];
    }
    for (int k = 0; k < 4; ++k) exp_s[k] /= sum;
    return exp_s;
}

// ── classify ──────────────────────────────────────────────────────────────────

RegimeProbabilities RegimeClassifier::classify(const MacroFeatures &z) const
{
    std::array<double, 4> scores{};

    for (int k = 0; k < 4; ++k) {
        scores[k] = weights_[k].intercept;
        for (const auto &[feat, w] : weights_[k].feature_weights) {
            auto it = z.find(feat);
            if (it != z.end())
                scores[k] += w * it->second;
        }
    }

    RegimeProbabilities rp;
    rp.probs = softmax(scores);
    return rp;
}

// ── signal_scalar ─────────────────────────────────────────────────────────────

double RegimeClassifier::signal_scalar(const MacroFeatures &z) const
{
    const auto rp = classify(z);
    double scalar = 0.0;
    for (int k = 0; k < 4; ++k)
        scalar += rp.probs[k] * kRegimeSignalScales[k];
    return scalar;
}

// ── RegimeProbabilities::dominant ────────────────────────────────────────────

int RegimeProbabilities::dominant() const
{
    return static_cast<int>(
        std::max_element(probs.begin(), probs.end()) - probs.begin());
}
