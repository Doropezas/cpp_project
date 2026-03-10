#pragma once

#include "core/MarketEvent.hpp"

#include <stdexcept>
#include <string>
#include <vector>

// Thrown when a CSV file cannot be opened or contains malformed data.
class CSVParseError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parses a single CSV file into a vector of MarketEvent objects.
//
// Expected header (case-sensitive):
//   symbol,date,open,high,low,close,volume
//
// Rules:
//   - Header row is required and must be the first line
//   - Lines beginning with '#' are skipped (comments)
//   - Empty lines are skipped
//   - Any row with the wrong number of fields throws CSVParseError
//   - Numeric fields that cannot be parsed throw CSVParseError
//
// Ownership: returns by value; caller owns the vector.

class CSVLoader {
public:
    // Load one CSV file. Throws CSVParseError on any format violation.
    [[nodiscard]] static std::vector<MarketEvent>
    load(const std::string& filepath);
};
