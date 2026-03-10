#include "backtest/ResultAggregator.hpp"
#include "core/ThreadPool.hpp"
#include "data/CSVLoader.hpp"
#include "data/MarketDataLoader.hpp"

#include <algorithm>
#include <format>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

static void print_summary(const std::vector<DailyBar>& bars) {
    std::unordered_map<std::string, std::vector<const DailyBar*>> by_symbol;
    for (const auto& bar : bars) by_symbol[bar.symbol].push_back(&bar);

    std::cout << std::format("\n{:<6}  {:>5}  {:>10}  {:>10}  {:>10}  {:>10}\n",
                             "SYMBOL", "BARS", "FIRST DATE", "LAST DATE",
                             "FIRST CLOSE", "LAST CLOSE");
    std::cout << std::string(62, '-') << '\n';

    std::vector<std::string> symbols;
    symbols.reserve(by_symbol.size());
    for (const auto& [sym, _] : by_symbol) symbols.push_back(sym);
    std::sort(symbols.begin(), symbols.end());

    for (const auto& sym : symbols) {
        const auto& sb = by_symbol[sym];
        std::cout << std::format("{:<6}  {:>5}  {:>10}  {:>10}  {:>10.2f}  {:>10.2f}\n",
                                 sym, sb.size(),
                                 sb.front()->date, sb.back()->date,
                                 sb.front()->close, sb.back()->close);
    }
    std::cout << '\n';
}

static void print_metrics(const PerformanceMetrics& m) {
    std::cout << std::format(
        "  {:<12}  sharpe={:>7.3f}  ret={:>7.4f}  drawdown={:>7.4f}"
        "  hit={:>5.1f}%  turnover={:>6.4f}  days={}\n",
        m.label, m.sharpe, m.total_return, m.max_drawdown,
        m.hit_ratio * 100.0, m.turnover, m.num_days);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const std::string data_path = (argc > 1) ? argv[1] : "data/sample";

    std::cout << std::format("Quant Research Engine — loading data from '{}' ...\n",
                             data_path);

    std::vector<DailyBar> all_bars;
    try {
        all_bars = MarketDataLoader::load_directory(data_path);
    } catch (const CSVParseError& e) {
        std::cerr << std::format("Parse error: {}\n", e.what());
        return 1;
    }

    if (all_bars.empty()) {
        std::cout << "No bars loaded. Check that CSV files exist in '"
                  << data_path << "'.\n";
        return 1;
    }

    std::cout << std::format("Loaded {} bars total.\n", all_bars.size());
    print_summary(all_bars);

    // Group bars by symbol
    std::unordered_map<std::string, std::vector<DailyBar>> by_symbol;
    for (const auto& bar : all_bars) by_symbol[bar.symbol].push_back(bar);

    // Determine number of worker threads: one per symbol, capped at hardware
    const std::size_t num_threads = std::min(
        by_symbol.size(),
        static_cast<std::size_t>(std::thread::hardware_concurrency()));

    std::cout << std::format("Running backtest on {} symbol(s) with {} thread(s) ...\n\n",
                             by_symbol.size(), num_threads);

    // Run all symbols in parallel via ThreadPool + ResultAggregator
    ThreadPool pool{num_threads};
    Backtester bt;
    ResultAggregator agg{pool, bt};

    for (auto& [sym, bars] : by_symbol) {
        agg.add_symbol(sym, std::move(bars), sym);
    }
    agg.wait_all();

    // Print per-symbol results
    std::cout << "=== Per-Symbol Performance ===\n";
    std::vector<std::string> symbols;
    for (const auto& [sym, _] : agg.symbol_results()) symbols.push_back(sym);
    std::sort(symbols.begin(), symbols.end());

    for (const auto& sym : symbols) {
        const auto& m = agg.symbol_results().at(sym).metrics;
        print_metrics(m);
    }

    // Print portfolio summary
    std::cout << "\n=== Portfolio Summary ===\n";
    print_metrics(agg.portfolio_summary("portfolio"));

    std::cout << '\n';
    return 0;
}
