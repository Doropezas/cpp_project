#pragma once

#include "core/DailyBar.hpp"

#include <string>
#include <vector>

// Loads all CSV files in a directory (or a single file) and converts
// MarketEvent records into DailyBar objects.
//
// Responsibilities:
//   - Delegates raw parsing to CSVLoader
//   - Groups events by symbol
//   - Sorts each symbol's bars chronologically
//   - Computes return_1d = log(close[t] / close[t-1])
//     (0.0 for the first bar of each symbol)
//   - Sets close_unadjusted = close (Stage 1: no Panama adjustment yet)
//   - Returns a flat vector of all DailyBar objects across all symbols

class MarketDataLoader {
public:
    // Load every *.csv file found directly under directory_path.
    // Files in subdirectories are not traversed.
    // Throws CSVParseError if any file is malformed.
    [[nodiscard]] static std::vector<DailyBar>
    load_directory(const std::string& directory_path);

    // Load a single CSV file.
    [[nodiscard]] static std::vector<DailyBar>
    load_file(const std::string& filepath);
};
