#include "data/CSVLoader.hpp"
#include "data/MarketDataLoader.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Write a temporary CSV file and return its path.
static std::string write_temp_csv(const std::string& filename,
                                  const std::string& content) {
    std::ofstream f{filename};
    assert(f.is_open());
    f << content;
    return filename;
}

static bool approx_equal(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) < tol;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_parse_valid_file() {
    const std::string path = "/tmp/test_valid.csv";
    write_temp_csv(path,
        "symbol,date,open,high,low,close,volume\n"
        "ES,2010-01-04,1132.25,1141.75,1128.75,1139.75,2187654\n"
        "ES,2010-01-05,1139.75,1145.75,1135.50,1143.75,1654321\n");

    auto events = CSVLoader::load(path);

    assert(events.size() == 2);
    assert(events[0].symbol == "ES");
    assert(events[0].date   == "2010-01-04");
    assert(approx_equal(events[0].open,   1132.25));
    assert(approx_equal(events[0].high,   1141.75));
    assert(approx_equal(events[0].low,    1128.75));
    assert(approx_equal(events[0].close,  1139.75));
    assert(approx_equal(events[0].volume, 2187654.0));

    std::cout << "PASS  test_parse_valid_file\n";
}

static void test_skip_comments_and_blank_lines() {
    const std::string path = "/tmp/test_comments.csv";
    write_temp_csv(path,
        "symbol,date,open,high,low,close,volume\n"
        "# this is a comment\n"
        "\n"
        "GC,2010-01-04,1087.50,1098.40,1084.70,1096.20,187654\n");

    auto events = CSVLoader::load(path);
    assert(events.size() == 1);
    assert(events[0].symbol == "GC");

    std::cout << "PASS  test_skip_comments_and_blank_lines\n";
}

static void test_wrong_header_throws() {
    const std::string path = "/tmp/test_bad_header.csv";
    write_temp_csv(path, "Symbol,Date,Open,High,Low,Close,Volume\n"
                         "ES,2010-01-04,1132.25,1141.75,1128.75,1139.75,2187654\n");

    bool threw{false};
    try {
        [[maybe_unused]] auto _ = CSVLoader::load(path);
    } catch (const CSVParseError&) {
        threw = true;
    }
    assert(threw);

    std::cout << "PASS  test_wrong_header_throws\n";
}

static void test_wrong_field_count_throws() {
    const std::string path = "/tmp/test_bad_fields.csv";
    write_temp_csv(path,
        "symbol,date,open,high,low,close,volume\n"
        "ES,2010-01-04,1132.25,1141.75\n");   // only 4 fields

    bool threw{false};
    try {
        [[maybe_unused]] auto _ = CSVLoader::load(path);
    } catch (const CSVParseError&) {
        threw = true;
    }
    assert(threw);

    std::cout << "PASS  test_wrong_field_count_throws\n";
}

static void test_non_numeric_field_throws() {
    const std::string path = "/tmp/test_bad_number.csv";
    write_temp_csv(path,
        "symbol,date,open,high,low,close,volume\n"
        "ES,2010-01-04,N/A,1141.75,1128.75,1139.75,2187654\n");

    bool threw{false};
    try {
        [[maybe_unused]] auto _ = CSVLoader::load(path);
    } catch (const CSVParseError&) {
        threw = true;
    }
    assert(threw);

    std::cout << "PASS  test_non_numeric_field_throws\n";
}

static void test_returns_computed_correctly() {
    const std::string path = "/tmp/test_returns.csv";
    write_temp_csv(path,
        "symbol,date,open,high,low,close,volume\n"
        "ES,2010-01-04,1100.00,1110.00,1090.00,1100.00,1000000\n"
        "ES,2010-01-05,1100.00,1120.00,1095.00,1210.00,1000000\n"
        "ES,2010-01-06,1210.00,1230.00,1200.00,1000.00,1000000\n");

    auto bars = MarketDataLoader::load_file(path);

    assert(bars.size() == 3);

    // First bar: return is 0
    assert(approx_equal(bars[0].return_1d, 0.0));

    // Second bar: log(1210 / 1100)
    const double expected_r1 = std::log(1210.0 / 1100.0);
    assert(approx_equal(bars[1].return_1d, expected_r1, 1e-10));

    // Third bar: log(1000 / 1210)
    const double expected_r2 = std::log(1000.0 / 1210.0);
    assert(approx_equal(bars[2].return_1d, expected_r2, 1e-10));

    // Stage 1: close_unadjusted == close
    assert(approx_equal(bars[0].close_unadjusted, bars[0].close));

    std::cout << "PASS  test_returns_computed_correctly\n";
}

static void test_multi_symbol_sorted() {
    const std::string path = "/tmp/test_multi.csv";
    // Deliberately out of date order within a symbol
    write_temp_csv(path,
        "symbol,date,open,high,low,close,volume\n"
        "GC,2010-01-05,1096.20,1100.80,1090.10,1092.30,165432\n"
        "ES,2010-01-04,1132.25,1141.75,1128.75,1139.75,2187654\n"
        "GC,2010-01-04,1087.50,1098.40,1084.70,1096.20,187654\n"
        "ES,2010-01-05,1139.75,1145.75,1135.50,1143.75,1654321\n");

    auto bars = MarketDataLoader::load_file(path);
    assert(bars.size() == 4);

    // Each symbol's first bar must have return_1d == 0.0
    for (const auto& bar : bars) {
        if (bar.date == "2010-01-04") {
            assert(approx_equal(bar.return_1d, 0.0));
        }
    }

    std::cout << "PASS  test_multi_symbol_sorted\n";
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    std::cout << "Running Stage 1 tests...\n\n";

    test_parse_valid_file();
    test_skip_comments_and_blank_lines();
    test_wrong_header_throws();
    test_wrong_field_count_throws();
    test_non_numeric_field_throws();
    test_returns_computed_correctly();
    test_multi_symbol_sorted();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
