#include "data/MarketDataLoader.hpp"
#include "data/CSVLoader.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// Convert a flat list of MarketEvents into DailyBar objects.
// Groups by symbol, sorts chronologically, computes return_1d.
static std::vector<DailyBar>
events_to_bars(std::vector<MarketEvent> events) {
    // Group by symbol
    std::unordered_map<std::string, std::vector<MarketEvent>> by_symbol;
    for (auto& ev : events) {
        by_symbol[ev.symbol].push_back(std::move(ev));
    }

    std::vector<DailyBar> bars;
    bars.reserve(events.size());

    for (auto& [symbol, sym_events] : by_symbol) {
        // Sort chronologically by date string (YYYY-MM-DD sorts lexicographically)
        std::sort(sym_events.begin(), sym_events.end(),
                  [](const MarketEvent& a, const MarketEvent& b) {
                      return a.date < b.date;
                  });

        double prev_close{0.0};

        for (const auto& ev : sym_events) {
            DailyBar bar;
            bar.symbol            = ev.symbol;
            bar.date              = ev.date;
            bar.open              = ev.open;
            bar.high              = ev.high;
            bar.low               = ev.low;
            bar.close             = ev.close;
            bar.close_unadjusted  = (ev.close_unadjusted != 0.0)
                                      ? ev.close_unadjusted
                                      : ev.close;
            bar.volume            = ev.volume;
            bar.is_roll_date      = ev.is_roll_date;

            // return_1d = log(close[t] / close[t-1])
            // First bar of each symbol: return is undefined, set to 0.0
            // Guard against non-positive prices (e.g. CL went negative Apr 2020)
            if (prev_close > 0.0 && ev.close > 0.0) {
                bar.return_1d = std::log(ev.close / prev_close);
            } else {
                bar.return_1d = 0.0;
            }

            prev_close = ev.close;
            bars.push_back(std::move(bar));
        }
    }

    return bars;
}

std::vector<DailyBar>
MarketDataLoader::load_file(const std::string& filepath) {
    auto events = CSVLoader::load(filepath);
    return events_to_bars(std::move(events));
}

std::vector<DailyBar>
MarketDataLoader::load_directory(const std::string& directory_path) {
    const fs::path dir{directory_path};
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        throw CSVParseError{
            "load_directory: '" + directory_path + "' is not a directory"};
    }

    std::vector<MarketEvent> all_events;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".csv") continue;

        auto file_events = CSVLoader::load(entry.path().string());
        all_events.insert(all_events.end(),
                          std::make_move_iterator(file_events.begin()),
                          std::make_move_iterator(file_events.end()));
    }

    return events_to_bars(std::move(all_events));
}
