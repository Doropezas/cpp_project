cmake_minimum_required(VERSION 3.20)
project(quant_research_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Library: core (thread pool, queue — header-only templates + ThreadPool.cpp)
add_library(core_lib
    src/core/ThreadPool.cpp
)
target_include_directories(core_lib PUBLIC include)

# ── Library: data layer ───────────────────────────────────────────────────────
add_library(data_lib
    src/data/CSVLoader.cpp
    src/data/MarketDataLoader.cpp
)
target_include_directories(data_lib PUBLIC include)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(quant_engine src/main.cpp)
target_link_libraries(quant_engine PRIVATE backtest_lib)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()

add_executable(test_csv tests/test_csv_loader.cpp)
target_link_libraries(test_csv PRIVATE data_lib)
add_test(NAME csv_loader COMMAND test_csv)

add_executable(test_queue tests/test_thread_safe_queue.cpp)
target_link_libraries(test_queue PRIVATE core_lib)
add_test(NAME thread_safe_queue COMMAND test_queue)

add_executable(test_pool tests/test_thread_pool.cpp)
target_link_libraries(test_pool PRIVATE core_lib)
add_test(NAME thread_pool COMMAND test_pool)

# ── Library: signals ──────────────────────────────────────────────────────────
add_library(signals_lib
    src/signals/MovingAverageSignal.cpp
    src/signals/MomentumSignal.cpp
    src/signals/VolatilitySignal.cpp
)
target_link_libraries(signals_lib PUBLIC data_lib core_lib)

add_executable(test_signals tests/test_signals.cpp)
target_link_libraries(test_signals PRIVATE signals_lib)
add_test(NAME signals COMMAND test_signals)

# ── Library: backtester ───────────────────────────────────────────────────────
add_library(backtest_lib
    src/backtest/Backtester.cpp
    src/backtest/ResultAggregator.cpp
)
target_link_libraries(backtest_lib PUBLIC signals_lib core_lib)

add_executable(test_backtester tests/test_backtester.cpp)
target_link_libraries(test_backtester PRIVATE backtest_lib)
add_test(NAME backtester COMMAND test_backtester)

add_executable(test_result_aggregator tests/test_result_aggregator.cpp)
target_link_libraries(test_result_aggregator PRIVATE backtest_lib)
add_test(NAME result_aggregator COMMAND test_result_aggregator)
