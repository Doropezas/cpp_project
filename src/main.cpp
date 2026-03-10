#include "backtest/Backtester.hpp"
#include "backtest/ExperimentConfig.hpp"
#include "backtest/ResultAggregator.hpp"
#include "core/Constants.hpp"
#include "core/ThreadPool.hpp"
#include "data/CSVLoader.hpp"
#include "data/MacroDataLoader.hpp"
#include "data/MarketDataLoader.hpp"
#include "signals/RegimeClassifier.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

static void print_data_summary(const std::vector<DailyBar> &bars)
{
    std::unordered_map<std::string, std::vector<const DailyBar *>> by_sym;
    for (const auto &b : bars)
        by_sym[b.symbol].push_back(&b);

    std::cout << std::format("\n{:<6}  {:>5}  {:>10}  {:>10}  {:>12}  {:>12}\n",
                             "SYMBOL", "BARS", "FIRST DATE", "LAST DATE",
                             "FIRST CLOSE", "LAST CLOSE");
    std::cout << std::string(64, '-') << '\n';

    std::vector<std::string> syms;
    for (const auto &[s, _] : by_sym)
        syms.push_back(s);
    std::sort(syms.begin(), syms.end());

    for (const auto &s : syms)
    {
        const auto &sb = by_sym[s];
        std::cout << std::format("{:<6}  {:>5}  {:>10}  {:>10}  {:>12.2f}  {:>12.2f}\n",
                                 s, sb.size(),
                                 sb.front()->date, sb.back()->date,
                                 sb.front()->close, sb.back()->close);
    }
    std::cout << '\n';
}

static void print_metrics(const PerformanceMetrics &m)
{
    std::cout << std::format(
        "  {:<20}  sharpe={:>7.3f}  ret={:>7.4f}  drawdown={:>7.4f}"
        "  hit={:>5.1f}%  turnover={:>6.4f}  days={}\n",
        m.label, m.sharpe, m.total_return, m.max_drawdown,
        m.hit_ratio * 100.0, m.turnover, m.num_days);
}

// Date Helpers

static std::vector<DailyPosition> filter_by_date(
    const std::vector<DailyPosition> &pos,
    const std::string &from, // "" = no lower bound
    const std::string &to)   // "" = no upper bound
{
    std::vector<DailyPosition> out;
    out.reserve(pos.size());
    for (const auto &dp : pos)
    {
        if (!from.empty() && dp.date <= from)
            continue; // strictly after from
        if (!to.empty() && dp.date > to)
            continue; // up to and including to
        out.push_back(dp);
    }
    return out;
}

// Rebase cum_pnl to start from 0.0 for a sub-period slice.
static void rebase_cum_pnl(std::vector<DailyPosition> &pos)
{
    double c = 0.0;
    for (auto &dp : pos)
    {
        c += dp.pnl;
        dp.cum_pnl = c;
    }
}

// Output

static void write_equity_curve(const std::vector<DailyPosition> &pos,
                               const std::string &filepath)
{
    std::ofstream f{filepath};
    if (!f.is_open())
        return;
    f << "date,cum_pnl\n";
    for (const auto &dp : pos)
        f << std::format("{},{:.6f}\n", dp.date, dp.cum_pnl);
}

static void write_positions_csv(const std::vector<DailyPosition> &pos,
                                const std::string &filepath)
{
    std::ofstream f{filepath};
    if (!f.is_open())
        return;
    f << "date,symbol,position,pnl,cum_pnl\n";
    for (const auto &dp : pos)
        f << std::format("{},{},{:.6f},{:.6f},{:.6f}\n",
                         dp.date, dp.symbol, dp.position, dp.pnl, dp.cum_pnl);
}

// Usage: quant_engine [data_path] [--regime] [--id <name>] [--output <dir>]

static ExperimentConfig parse_args(int argc, char *argv[])
{
    ExperimentConfig cfg;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg{argv[i]};
        if (arg == "--regime")
            cfg.use_regime = true;
        else if (arg == "--id" && i + 1 < argc)
            cfg.id = argv[++i];
        else if (arg == "--output" && i + 1 < argc)
            cfg.output_dir = argv[++i];
        else if (arg[0] != '-')
            cfg.data_path = arg;
    }
    return cfg;
}

int main(int argc, char *argv[])
{
    const ExperimentConfig cfg = parse_args(argc, argv);

    std::cout << std::format("Quant Research Engine — id='{}'\n", cfg.id);
    std::cout << std::format("  data='{}'  split='{}'  regime={}\n",
                             cfg.data_path, cfg.split_date,
                             cfg.use_regime ? "on" : "off");

    // Load market data
    std::vector<DailyBar> all_bars;
    try
    {
        all_bars = MarketDataLoader::load_directory(cfg.data_path);
    }
    catch (const CSVParseError &e)
    {
        std::cerr << std::format("Parse error: {}\n", e.what());
        return 1;
    }

    if (all_bars.empty())
    {
        std::cerr << "No bars loaded. Check CSV files in '" << cfg.data_path << "'.\n";
        return 1;
    }

    std::cout << std::format("Loaded {} bars total.\n", all_bars.size());
    print_data_summary(all_bars);

    // Macro Panel
    MacroPanel macro_panel;
    if (cfg.use_regime)
    {
        macro_panel = MacroPanel::load(cfg.macro_path);
        if (macro_panel.empty())
            std::cout << std::format("  Warning: macro panel not found at '{}' — "
                                     "regime scaling disabled.\n",
                                     cfg.macro_path);
        else
            std::cout << std::format("  Macro panel: {} dates loaded.\n",
                                     macro_panel.size());
    }

    const bool use_regime = cfg.use_regime && !macro_panel.empty();
    const MacroPanel *macro_ptr = use_regime ? &macro_panel : nullptr;
    RegimeClassifier regime_clf;
    const RegimeClassifier *regime_ptr = use_regime ? &regime_clf : nullptr;

    // Group bars by symbol
    std::unordered_map<std::string, std::vector<DailyBar>> by_symbol;
    for (const auto &bar : all_bars)
        by_symbol[bar.symbol].push_back(bar);

    const std::size_t num_threads = std::min(
        by_symbol.size(),
        static_cast<std::size_t>(std::thread::hardware_concurrency()));

    std::cout << std::format("Running {} symbol(s) × {} thread(s) ...\n\n",
                             by_symbol.size(), num_threads);

    // Regime path: submit directly through pool (macro/regime pointers needed).
    // Baseline path: use ResultAggregator which also provides portfolio_pnl().

    Backtester bt{cfg.ma_fast, cfg.ma_slow, cfg.mom_lb, cfg.mom_skip};
    ThreadPool pool{num_threads};

    std::unordered_map<std::string, Backtester::RunResult> sym_results;

    if (use_regime)
    {
        // Submit directly — each task captures the extra pointers.
        std::vector<std::pair<std::string,
                              std::future<Backtester::RunResult>>>
            futures;

        for (auto &[sym, bars] : by_symbol)
        {
            auto fut = pool.submit(
                [&bt, sym = sym, bars = std::move(bars), macro_ptr, regime_ptr]() mutable
                {
                    return bt.run(bars, sym, macro_ptr, regime_ptr);
                });
            futures.push_back({sym, std::move(fut)});
        }
        for (auto &[sym, fut] : futures)
            sym_results[sym] = fut.get();
    }
    else
    {
        ResultAggregator agg{pool, bt};
        for (auto &[sym, bars] : by_symbol)
            agg.add_symbol(sym, std::move(bars), sym);
        agg.wait_all();

        // Print cross-symbol inv-vol portfolio before IS/OOS split.
        std::cout << "=== Portfolio (inv-vol cross-symbol) ===\n";
        print_metrics(agg.portfolio_pnl("portfolio_inv_vol"));
        std::cout << "\n=== Portfolio (equal-weight avg) ===\n";
        print_metrics(agg.portfolio_summary("portfolio_eq_wt"));
        std::cout << '\n';

        for (const auto &[sym, run] : agg.symbol_results())
            sym_results[sym] = run;
    }

    // Per Symbol
    std::vector<std::string> all_syms;
    for (const auto &[sym, _] : sym_results)
        all_syms.push_back(sym);
    std::sort(all_syms.begin(), all_syms.end());

    std::cout << "=== Per-Symbol Performance (full period) ===\n";
    for (const auto &sym : all_syms)
        print_metrics(sym_results.at(sym).metrics);

    // IS - OOS
    std::cout << "\n=== IS / OOS Split (split date: " << cfg.split_date << ") ===\n";
    std::cout << std::format("{:<6}  {:>8}  {:>8}  {:>8}  {:>8}  {:>8}  {:>8}\n",
                             "SYM", "IS-SHP", "OOS-SHP", "IS-RET", "OOS-RET",
                             "IS-DD", "OOS-DD");
    std::cout << std::string(68, '-') << '\n';

    double is_sum = 0.0, oos_sum = 0.0;
    int count = 0;

    for (const auto &sym : all_syms)
    {
        const auto &positions = sym_results.at(sym).positions;

        auto is_pos = filter_by_date(positions, "", cfg.split_date);
        auto oos_pos = filter_by_date(positions, cfg.split_date, "");

        rebase_cum_pnl(oos_pos);

        const auto is_m = Backtester::compute_metrics(is_pos, sym + "_IS");
        const auto oos_m = Backtester::compute_metrics(oos_pos, sym + "_OOS");

        std::cout << std::format("{:<6}  {:>8.3f}  {:>8.3f}  {:>8.4f}  {:>8.4f}"
                                 "  {:>8.4f}  {:>8.4f}\n",
                                 sym,
                                 is_m.sharpe, oos_m.sharpe,
                                 is_m.total_return, oos_m.total_return,
                                 is_m.max_drawdown, oos_m.max_drawdown);

        is_sum += is_m.sharpe;
        oos_sum += oos_m.sharpe;
        ++count;
    }

    if (count > 0)
    {
        std::cout << std::string(68, '-') << '\n';
        std::cout << std::format("{:<6}  {:>8.3f}  {:>8.3f}  (avg Sharpe)\n",
                                 "AVG", is_sum / count, oos_sum / count);
    }

    // Output
    const fs::path out_dir = fs::path(cfg.output_dir) / cfg.id;
    fs::create_directories(out_dir);

    for (const auto &sym : all_syms)
    {
        const auto &pos = sym_results.at(sym).positions;
        write_equity_curve(pos, (out_dir / (sym + "_equity.csv")).string());
        write_positions_csv(pos, (out_dir / (sym + "_positions.csv")).string());
    }

    std::cout << std::format("\nArtifacts written to '{}'\n\n", out_dir.string());
    return 0;
}
