#pragma once

#include <string>

// A single day's OHLCV record for one futures symbol.
//
// close          — Panama back-adjusted close (equals close_unadjusted for
//                  raw sample data loaded in Stage 1; updated by the
//                  continuous-series builder in later stages)
// close_unadjusted — raw contract close, never modified
// return_1d      — log(close[t] / close[t-1]) on the adjusted series;
//                  set to 0.0 for the first bar of each symbol
// is_roll_date   — true when this bar coincides with a contract roll event

struct DailyBar {
    std::string symbol;
    std::string date;           // "YYYY-MM-DD"
    double open{};
    double high{};
    double low{};
    double close{};
    double close_unadjusted{};
    double volume{};
    double return_1d{};
    bool   is_roll_date{false};
};
