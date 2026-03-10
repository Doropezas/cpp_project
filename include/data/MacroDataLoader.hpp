#pragma once

#include <string>
#include <unordered_map>

// MacroPanel
//
// Holds the date-indexed macro feature panel produced by scripts/build_macro_panel.py.
//
// Feature naming convention (RESEARCH.md §8):
//   {SERIES_ID}_z     — rolling 252-day z-score of the level
//   {SERIES_ID}_d20_z — rolling 252-day z-score of the 20-day difference
//
// Example features: DGS10_z, T10YIE_z, BAA10Y_z, VIX_z, DGS10_d20_z, ...
//
// Usage:
//   auto panel = MacroPanel::load("data/processed/macro/macro_panel.csv");
//   const auto* z = panel.get("2021-06-15");
//   if (z) double vix_z = z->at("VIX_z");

using MacroFeatures = std::unordered_map<std::string, double>;

class MacroPanel
{
public:
    // Load from CSV produced by build_macro_panel.py.
    // Returns an empty panel (not an exception) if the file does not exist,
    // so callers can degrade gracefully when macro data has not been built.
    [[nodiscard]] static MacroPanel load(const std::string &filepath);

    // Returns nullptr if the date is not in the panel.
    [[nodiscard]] const MacroFeatures *get(const std::string &date) const;

    [[nodiscard]] bool        empty() const { return data_.empty(); }
    [[nodiscard]] std::size_t size()  const { return data_.size();  }

private:
    // date ("YYYY-MM-DD") → feature map
    std::unordered_map<std::string, MacroFeatures> data_;
};
