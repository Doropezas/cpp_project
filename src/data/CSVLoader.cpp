#include "data/CSVLoader.hpp"

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Split a line on commas. Returns all fields including empty ones.
static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::istringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

// Parse a double, throwing CSVParseError with context on failure.
static double parse_double(const std::string& s,
                           const std::string& field_name,
                           int line_number) {
    try {
        std::size_t pos{};
        double val = std::stod(s, &pos);
        if (pos != s.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return val;
    } catch (const std::exception&) {
        throw CSVParseError{
            std::format("line {}: cannot parse '{}' as double for field '{}'",
                        line_number, s, field_name)};
    }
}

std::vector<MarketEvent> CSVLoader::load(const std::string& filepath) {
    std::ifstream file{filepath};
    if (!file.is_open()) {
        throw CSVParseError{std::format("cannot open file: '{}'", filepath)};
    }

    std::vector<MarketEvent> events;
    std::string line;
    int line_number{0};
    bool header_seen{false};

    // Supported header layouts:
    //   7-col: symbol,date,open,high,low,close,volume
    //   9-col: symbol,date,open,high,low,close,close_unadjusted,volume,is_roll_date
    bool extended_format = false;

    while (std::getline(file, line)) {
        ++line_number;

        // Strip trailing \r (Windows / Python csv CRLF line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        if (!header_seen) {
            const std::string h7 = "symbol,date,open,high,low,close,volume";
            const std::string h9 = "symbol,date,open,high,low,close,close_unadjusted,volume,is_roll_date";
            if (line == h9) {
                extended_format = true;
            } else if (line != h7) {
                throw CSVParseError{
                    std::format("line {}: expected header '{}', got '{}'",
                                line_number, h7, line)};
            }
            header_seen = true;
            continue;
        }

        auto fields = split_csv(line);
        const std::size_t expected_cols = extended_format ? 9u : 7u;
        if (fields.size() != expected_cols) {
            throw CSVParseError{
                std::format("line {}: expected {} fields, got {}",
                            line_number, expected_cols, fields.size())};
        }

        MarketEvent ev;
        ev.symbol = fields[0];
        ev.date   = fields[1];
        ev.open   = parse_double(fields[2], "open",   line_number);
        ev.high   = parse_double(fields[3], "high",   line_number);
        ev.low    = parse_double(fields[4], "low",    line_number);
        ev.close  = parse_double(fields[5], "close",  line_number);

        if (extended_format) {
            ev.close_unadjusted = parse_double(fields[6], "close_unadjusted", line_number);
            ev.volume           = parse_double(fields[7], "volume",           line_number);
            ev.is_roll_date     = (fields[8] == "1");
        } else {
            ev.close_unadjusted = ev.close;
            ev.volume           = parse_double(fields[6], "volume", line_number);
        }

        events.push_back(std::move(ev));
    }

    if (!header_seen && !events.empty()) {
        throw CSVParseError{
            std::format("'{}': file has data but no header row", filepath)};
    }

    return events;
}
