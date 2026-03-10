#pragma once

#include <string>

// Lightweight typed record produced directly from a CSV row.
// Used as an intermediate step before building DailyBar objects.
// The MarketDataLoader converts MarketEvent -> DailyBar (computing returns,
// flagging rolls, etc.).

struct MarketEvent
{
    std::string symbol;
    std::string date; // "YYYY-MM-DD"
    double open{};
    double high{};
    double low{};
    double close{};
    double volume{};
    double close_unadjusted{}; // raw contract close (0 if not present)
    bool is_roll_date{false};  // true on contract roll days
};
