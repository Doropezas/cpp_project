#include "data/MacroDataLoader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

// Parse the CSV produced by scripts/build_macro_panel.py.
// First row: header  →  date,feature1,feature2,...
// Subsequent rows:   date_value,val1,val2,...

MacroPanel MacroPanel::load(const std::string &filepath)
{
    MacroPanel panel;

    std::ifstream file{filepath};
    if (!file.is_open())
        return panel;   // graceful degradation — caller checks panel.empty()

    std::string line;

    // Parse header to get feature names.
    if (!std::getline(file, line)) return panel;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::vector<std::string> columns;
    {
        std::istringstream ss{line};
        std::string col;
        while (std::getline(ss, col, ',')) {
            if (!col.empty() && col.back() == '\r') col.pop_back();
            columns.push_back(col);
        }
    }

    if (columns.empty() || columns[0] != "date") return panel;

    // Parse data rows.
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::istringstream ss{line};
        std::string cell;
        std::vector<std::string> cells;

        while (std::getline(ss, cell, ',')) {
            if (!cell.empty() && cell.back() == '\r') cell.pop_back();
            cells.push_back(cell);
        }

        if (cells.size() != columns.size()) continue;

        const std::string &date = cells[0];
        MacroFeatures features;
        features.reserve(columns.size() - 1);

        for (std::size_t i = 1; i < columns.size(); ++i) {
            try {
                features[columns[i]] = std::stod(cells[i]);
            } catch (...) {
                features[columns[i]] = 0.0;   // NaN / missing → 0 (neutral)
            }
        }

        panel.data_[date] = std::move(features);
    }

    return panel;
}

const MacroFeatures *MacroPanel::get(const std::string &date) const
{
    auto it = data_.find(date);
    return (it != data_.end()) ? &it->second : nullptr;
}
