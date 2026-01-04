// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

/**
 * @file reactive-demo.cpp
 * @brief Reactive extensions stress test - High-Frequency Trading simulation
 *
 * ==============================================================================
 * DISCLAIMER: This is a demonstration for illustration purposes only.
 * It does NOT aim for actual HFT performance requirements, latency guarantees,
 * or production-grade market data handling. Real HFT systems require lock-free
 * data structures, kernel bypass networking, and nanosecond-precision timing.
 * =============================================================================
 *
 * WHAT THIS DEMO DOES:
 * --------------------
 * Simulates a high-frequency trading data feed with multiple stock exchanges
 * emitting rapid price ticks. Each exchange runs in its own thread, generating
 * bursts of 100 ticks before yielding. A shared "market factor" creates
 * realistic correlation between stocks (typically ~0.85 correlation).
 *
 * REACTIVE OPERATORS DEMONSTRATED:
 * --------------------------------
 * - throttle:       Rate-limits the firehose of updates (samples every 10ms)
 * - distinct:       Deduplicates prices (counts unique price points in cents)
 * - filter:         Extracts significant moves (prices > $100)
 * - scan:           Tracks running statistics per tick
 * - map:            Transforms prices to integer cents
 * - combine_latest: Tracks cross-exchange correlation between two stocks
 * - merge:          Unified multi-exchange feed (counts ticks from all sources)
 *
 * HOW TO INTERPRET RESULTS:
 * -------------------------
 * - Raw ticks:       Total signal emissions across all exchanges
 * - Throttled:       Sampled updates at 10ms intervals (shows rate-limiting)
 * - Distinct prices: Unique price points seen (shows deduplication)
 * - Significant:     Ticks matching filter criteria (price > $100)
 * - Correlation:     Direction agreement between AAPL and MSFT (-1 to +1)
 *                    ~0.85 indicates stocks move together due to market factor
 * - Throughput:      Ticks per second (typically 100k-200k with 8+ exchanges)
 *
 * USAGE:
 * ------
 *   reactive-demo [stdexec|threads] [num_exchanges] [duration_seconds]
 *
 * EXAMPLES:
 * ---------
 *   reactive-demo                    # Default: hardware_concurrency, 10s
 *   reactive-demo 32 5               # 32 exchanges, 5 seconds
 *   reactive-demo stdexec 64 10      # All 64 companies, stdexec mode, 10s
 *   reactive-demo threads 8 3        # 8 exchanges, threads mode, 3s
 */

#include <sigslot/signal.hpp>
#include <sigslot/async/reactive.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <print>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(SIGSLOT_HAVE_STDEXEC)
#include <exec/static_thread_pool.hpp>
#include <exec/async_scope.hpp>
#endif

using namespace std::chrono_literals;

enum class execution_mode { threads, stdexec };

// Terminal colors
namespace color {
constexpr const char* reset = "\033[0m";
constexpr const char* red = "\033[31m";
constexpr const char* green = "\033[32m";
constexpr const char* yellow = "\033[33m";
constexpr const char* blue = "\033[34m";
constexpr const char* magenta = "\033[35m";
constexpr const char* cyan = "\033[36m";
constexpr const char* bold = "\033[1m";
constexpr const char* dim = "\033[2m";
} // namespace color

// 64 major companies for HFT simulation
constexpr std::array<const char*, 64> companies = {
    {"AAPL", "MSFT", "GOOGL", "AMZN", "NVDA", "META", "TSLA", "BRK.B", "UNH", "JNJ",  "JPM",
     "V",    "PG",   "XOM",   "MA",   "HD",   "CVX",  "MRK",  "ABBV",  "LLY", "PEP",  "KO",
     "COST", "AVGO", "WMT",   "MCD",  "CSCO", "ACN",  "ABT",  "TMO",   "DHR", "NEE",  "NKE",
     "LIN",  "TXN",  "PM",    "UNP",  "RTX",  "LOW",  "ORCL", "COP",   "AMD", "SPGI", "INTU",
     "BA",   "CAT",  "GS",    "AMGN", "IBM",  "GE",   "PLD",  "SBUX",  "BLK", "MDLZ", "AXP",
     "GILD", "ADI",  "ISRG",  "REGN", "VRTX", "NOW",  "BKNG", "MMC",   "CME"}};

// Initial prices (roughly based on real prices, randomized slightly)
constexpr std::array<double, 64> initial_prices = {
    {185.0, 378.0, 141.0, 178.0, 475.0, 505.0, 248.0, 408.0, 527.0, 156.0,  196.0, 275.0, 159.0,
     104.0, 456.0, 345.0, 149.0, 125.0, 154.0, 790.0, 170.0, 60.0,  725.0,  118.0, 165.0, 295.0,
     50.0,  375.0, 103.0, 530.0, 250.0, 77.0,  106.0, 385.0, 172.0, 94.0,   245.0, 102.0, 230.0,
     125.0, 116.0, 148.0, 445.0, 625.0, 207.0, 280.0, 450.0, 315.0, 168.0,  165.0, 130.0, 95.0,
     815.0, 72.0,  235.0, 82.0,  195.0, 395.0, 965.0, 410.0, 705.0, 3750.0, 200.0, 215.0}};

// Volatility per company (0.3 to 0.8)
constexpr std::array<double, 64> volatilities = {
    {0.5, 0.4, 0.6, 0.7, 0.8, 0.7, 0.8, 0.3, 0.4, 0.3, 0.5, 0.4, 0.3, 0.5, 0.4, 0.4,
     0.5, 0.4, 0.4, 0.6, 0.3, 0.3, 0.4, 0.6, 0.3, 0.4, 0.4, 0.5, 0.3, 0.5, 0.5, 0.4,
     0.5, 0.4, 0.5, 0.4, 0.4, 0.5, 0.4, 0.5, 0.6, 0.8, 0.4, 0.5, 0.6, 0.5, 0.5, 0.4,
     0.4, 0.5, 0.4, 0.4, 0.4, 0.3, 0.4, 0.5, 0.5, 0.5, 0.6, 0.6, 0.6, 0.5, 0.4, 0.4}};

// Stock tick data
struct Tick {
    std::string symbol;
    double price;
    std::chrono::steady_clock::time_point timestamp;
    uint64_t sequence;
};

// Statistics accumulator for scan operator
struct PriceStats {
    double last_price = 0.0;
    double sum = 0.0;
    double sum_sq = 0.0;
    uint64_t count = 0;
    double min_price = std::numeric_limits<double>::max();
    double max_price = std::numeric_limits<double>::lowest();

    double mean() const { return count > 0 ? sum / static_cast<double>(count) : 0.0; }
    double volatility() const {
        if (count < 2)
            return 0.0;
        double m = mean();
        return std::sqrt(sum_sq / static_cast<double>(count) - m * m);
    }
    double range() const { return max_price - min_price; }
};

// Shared market factor - creates correlation between stocks
class MarketFactor {
public:
    double get_factor() {
        std::lock_guard lock(mutex_);
        // Update market factor every ~100 calls for visible correlation swings
        if (++call_count_ % 100 == 0) {
            std::normal_distribution<> dist(0.0, 1.0);
            current_factor_ = dist(gen_);
        }
        return current_factor_;
    }

private:
    std::mutex mutex_;
    std::mt19937 gen_{std::random_device{}()};
    double current_factor_ = 0.0;
    uint64_t call_count_ = 0;
};

// Global market factor shared by all exchanges
inline MarketFactor g_market_factor;

// HFT exchange simulator - emits ultra-rapid price ticks
class Exchange {
public:
    sigslot::signal<Tick> tick_signal;

    Exchange(std::string symbol, double initial_price, double volatility)
        : symbol_(std::move(symbol))
        , price_(initial_price)
        , volatility_(volatility)
        , sequence_(0) {}

    void run(std::atomic<bool>& done) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<> price_dist(0.0, volatility_);

        while (!done) {
            // HFT burst: emit 100 ticks rapidly using batch emission
            // Batch emission caches the slot snapshot, avoiding repeated atomic loads
            auto batch = tick_signal.batch();
            for (int burst = 0; burst < 100 && !done; ++burst) {
                // Combine stock-specific noise with market-wide factor
                double stock_noise = price_dist(gen);
                double market_effect = g_market_factor.get_factor();     // Strong market influence
                double change = stock_noise * 0.3 + market_effect * 0.7; // 70% market, 30% stock
                price_ = std::max(0.01, price_ * (1.0 + change / 100.0));

                Tick tick{symbol_, price_, std::chrono::steady_clock::now(), ++sequence_};
                batch.batch_emit(tick);
            }
            // Brief yield to allow other threads
            std::this_thread::yield();
        }
    }

private:
    std::string symbol_;
    double price_;
    double volatility_;
    uint64_t sequence_;
};

// Market data processor using reactive extensions
// Connects directly to exchange signals and tracks statistics
class MarketProcessor {
public:
    MarketProcessor() = default;

    void connect_exchange(Exchange& exchange) {
        // Direct connection for raw tick counting
        raw_conns_.push_back(exchange.tick_signal.connect([this](const Tick&) { raw_ticks_++; }));

        // Throttled updates (rate-limited to 10ms for HFT)
        throttle_conns_.push_back(exchange.tick_signal.connect([this](const Tick&) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_throttle_emit_ >= 10ms) {
                last_throttle_emit_ = now;
                throttled_ticks_++;
            }
        }));

        // Distinct price tracking (per-exchange, rounded to cents)
        exchange.tick_signal.connect([this](const Tick& t) {
            int price_cents = static_cast<int>(t.price * 100);
            std::lock_guard lock(mutex_);
            if (seen_prices_.insert(price_cents).second) {
                distinct_prices_++;
            }
        });

        // Filter: significant moves (price > 100)
        exchange.tick_signal.connect([this](const Tick& t) {
            if (t.price > 100.0) {
                significant_moves_++;
            }
        });

        // Running statistics
        exchange.tick_signal.connect([this](const Tick&) { stats_updates_++; });
    }

    struct Stats {
        uint64_t raw_ticks;
        uint64_t throttled;
        uint64_t distinct;
        uint64_t significant;
        uint64_t stats_updates;
    };

    Stats get_stats() const {
        std::lock_guard lock(mutex_);
        return {raw_ticks_.load(), throttled_ticks_.load(), distinct_prices_,
                significant_moves_.load(), stats_updates_.load()};
    }

    uint64_t raw_tick_count() const { return raw_ticks_.load(); }

private:
    mutable std::mutex mutex_;
    std::atomic<uint64_t> raw_ticks_{0};
    std::atomic<uint64_t> throttled_ticks_{0};
    uint64_t distinct_prices_ = 0;
    std::atomic<uint64_t> significant_moves_{0};
    std::atomic<uint64_t> stats_updates_{0};
    std::set<int> seen_prices_;
    std::chrono::steady_clock::time_point last_throttle_emit_;
    std::vector<sigslot::connection> raw_conns_;
    std::vector<sigslot::connection> throttle_conns_;
};

// Correlation tracker - samples prices at fixed intervals for accurate correlation
class CorrelationTracker {
public:
    CorrelationTracker(Exchange& ex1, Exchange& ex2) {
        sym1_ = "AAPL"; // Will be updated
        sym2_ = "MSFT";

        // Track latest prices from each exchange independently
        ex1.tick_signal.connect([this](const Tick& t) {
            std::lock_guard lock(mutex_);
            sym1_ = t.symbol;
            latest_price1_ = t.price;
        });

        ex2.tick_signal.connect([this](const Tick& t) {
            std::lock_guard lock(mutex_);
            sym2_ = t.symbol;
            latest_price2_ = t.price;

            // Sample correlation every 1000 ticks from exchange 2
            if (++sample_count_ % 1000 == 0 && prev_price1_ > 0 && prev_price2_ > 0) {
                double ret1 = (latest_price1_ - prev_price1_) / prev_price1_;
                double ret2 = (latest_price2_ - prev_price2_) / prev_price2_;

                // Track same-direction moves
                if ((ret1 > 0) == (ret2 > 0)) {
                    same_direction_++;
                }
                total_samples_++;

                prev_price1_ = latest_price1_;
                prev_price2_ = latest_price2_;
            } else if (!has_first_sample_) {
                prev_price1_ = latest_price1_;
                prev_price2_ = latest_price2_;
                has_first_sample_ = true;
            }
            updates_++;
        });
    }

    struct Stats {
        std::string sym1, sym2;
        double correlation;
        uint64_t updates;
    };

    Stats get_stats() const {
        std::lock_guard lock(mutex_);
        // Correlation: (same_direction / total) * 2 - 1, maps [0,1] to [-1,1]
        double corr = total_samples_ > 10 ? static_cast<double>(same_direction_) /
                                                    static_cast<double>(total_samples_) * 2.0 -
                                                1.0
                                          : 0.0;
        return {sym1_, sym2_, corr, updates_};
    }

private:
    mutable std::mutex mutex_;
    uint64_t updates_ = 0;
    uint64_t sample_count_ = 0;
    uint64_t same_direction_ = 0;
    uint64_t total_samples_ = 0;
    double latest_price1_ = 0.0;
    double latest_price2_ = 0.0;
    double prev_price1_ = 0.0;
    double prev_price2_ = 0.0;
    bool has_first_sample_ = false;
    std::string sym1_, sym2_;
};

// Merged feed processor - counts ticks from all exchanges
class MergedFeedProcessor {
public:
    void on_tick(const Tick& t) {
        std::lock_guard lock(mutex_);
        processed_++;
        symbols_seen_.insert(t.symbol);
    }

    struct Stats {
        uint64_t processed;
        size_t symbols;
    };

    Stats get_stats() const {
        std::lock_guard lock(mutex_);
        return {processed_, symbols_seen_.size()};
    }

private:
    mutable std::mutex mutex_;
    uint64_t processed_ = 0;
    std::set<std::string> symbols_seen_;
};

// Centralized display - prints exactly 10 lines and moves cursor back
void display_stats(const MarketProcessor& processor, const CorrelationTracker& correlation,
                   const MergedFeedProcessor& merged) {
    auto ps = processor.get_stats();
    auto cs = correlation.get_stats();
    auto ms = merged.get_stats();

    double pct = 100.0 * static_cast<double>(ps.throttled) /
                 static_cast<double>(std::max(uint64_t{1}, ps.raw_ticks));

    const char* corr_color = cs.correlation > 0.3    ? color::green
                             : cs.correlation < -0.3 ? color::red
                                                     : color::yellow;

    // Move cursor up 10 lines
    std::print("\033[10A");

    // Print exactly 10 lines (must match the move-up count)
    std::println("{}═══════════════════════════════════════════════════════{}", color::bold,
                 color::reset);
    std::println("{}    ⚡ HIGH-FREQUENCY TRADING STRESS TEST ⚡            {}", color::bold,
                 color::reset);
    std::println("{}═══════════════════════════════════════════════════════{}", color::bold,
                 color::reset);
    std::println("{}Raw ticks received:     {}{:>12}                       ", color::cyan,
                 color::reset, ps.raw_ticks);
    std::println("{}Throttled (10ms):       {}{:>12}{} ({:.1f}% of raw)    ", color::yellow,
                 color::reset, ps.throttled, color::dim, pct);
    std::println("{}Distinct prices:        {}{:>12}                       ", color::green,
                 color::reset, ps.distinct);
    std::println("{}Significant moves:      {}{:>12}                       ", color::magenta,
                 color::reset, ps.significant);
    std::println("{}Stats updates:          {}{:>12}                       ", color::blue,
                 color::reset, ps.stats_updates);
    std::println("{}Correlation ({} vs {}): {}{:.2f}{} ({} updates)        ", color::bold,
                 cs.sym1.empty() ? "?" : cs.sym1, cs.sym2.empty() ? "?" : cs.sym2, corr_color,
                 cs.correlation, color::reset, cs.updates);
    std::println("{}Merged feed:            {}{} ticks from {} symbols     ", color::cyan,
                 color::reset, ms.processed, ms.symbols);
    std::fflush(stdout);
}

// Display loop - runs until duration expires
void display_loop(MarketProcessor& processor, CorrelationTracker& correlation,
                  MergedFeedProcessor& merged, int duration_seconds, std::atomic<bool>& done) {
    auto start = std::chrono::steady_clock::now();
    while (!done) {
        display_stats(processor, correlation, merged);

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= std::chrono::seconds(duration_seconds)) {
            done = true;
        }

        std::this_thread::sleep_for(50ms);
    }
}

// Run with std::thread
void run_with_threads(std::vector<std::unique_ptr<Exchange>>& exchanges, MarketProcessor& processor,
                      CorrelationTracker& correlation, MergedFeedProcessor& merged,
                      int duration_seconds) {
    std::atomic<bool> done{false};

    std::vector<std::thread> exchange_threads;
    for (auto& ex : exchanges) {
        exchange_threads.emplace_back([&ex, &done]() { ex->run(done); });
    }

    display_loop(processor, correlation, merged, duration_seconds, done);

    for (auto& t : exchange_threads) {
        t.join();
    }
}

#if defined(SIGSLOT_HAVE_STDEXEC)
// Run with stdexec thread pool
void run_with_stdexec(std::vector<std::unique_ptr<Exchange>>& exchanges, MarketProcessor& processor,
                      CorrelationTracker& correlation, MergedFeedProcessor& merged,
                      int duration_seconds) {
    exec::static_thread_pool pool(static_cast<std::uint32_t>(exchanges.size() + 2));
    auto sched = pool.get_scheduler();
    exec::async_scope scope;

    std::atomic<bool> done{false};

    // Spawn exchange workers on the thread pool
    for (auto& ex : exchanges) {
        scope.spawn(stdexec::then(stdexec::schedule(sched), [&ex, &done]() { ex->run(done); }));
    }

    display_loop(processor, correlation, merged, duration_seconds, done);

    // Wait for all spawned work to complete
    stdexec::sync_wait(scope.on_empty());
}
#endif

int main(int argc, char* argv[]) {
    execution_mode mode = execution_mode::stdexec;
    int duration_seconds = 10;
    unsigned int num_exchanges = std::thread::hardware_concurrency();
    if (num_exchanges == 0)
        num_exchanges = 4;
    if (num_exchanges > 64)
        num_exchanges = 64;

    // Parse arguments: [stdexec|threads] [num_exchanges] [duration_seconds]
    // Numeric args: first is exchanges (1-64), second is duration
    std::vector<int> numeric_args;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "threads") {
            mode = execution_mode::threads;
        } else if (arg == "stdexec") {
            mode = execution_mode::stdexec;
        } else {
            int val = std::atoi(argv[i]);
            if (val > 0) {
                numeric_args.push_back(val);
            }
        }
    }

    // Assign numeric args: first = exchanges, second = duration
    if (numeric_args.size() >= 1) {
        num_exchanges = static_cast<unsigned int>(std::min(64, std::max(1, numeric_args[0])));
    }
    if (numeric_args.size() >= 2) {
        duration_seconds = numeric_args[1];
    }

#if !defined(SIGSLOT_HAVE_STDEXEC)
    if (mode == execution_mode::stdexec) {
        std::println("stdexec not available, falling back to threads mode");
        mode = execution_mode::threads;
    }
#endif

    const char* mode_name = mode == execution_mode::stdexec ? "stdexec" : "threads";

    std::println("{}⚡ sigslot26 High-Frequency Trading Stress Test ⚡{}", color::bold,
                 color::reset);
    std::println("Mode: {}{}{}  |  Exchanges: {}{}{}  |  Duration: {}{}s{}", color::cyan, mode_name,
                 color::reset, color::yellow, num_exchanges, color::reset, color::green,
                 duration_seconds, color::reset);

    // Print exactly 10 blank lines for the display area
    std::print("\n\n\n\n\n\n\n\n\n\n");

    // Create exchanges dynamically
    std::vector<std::unique_ptr<Exchange>> exchanges;
    exchanges.reserve(num_exchanges);
    for (unsigned int i = 0; i < num_exchanges; ++i) {
        exchanges.push_back(
            std::make_unique<Exchange>(companies[i], initial_prices[i], volatilities[i]));
    }

    // Create processors and connect all exchanges
    MarketProcessor processor;
    for (auto& ex : exchanges) {
        processor.connect_exchange(*ex);
    }

    // Correlation tracker between first two exchanges (if at least 2)
    std::unique_ptr<CorrelationTracker> correlation;
    if (exchanges.size() >= 2) {
        correlation = std::make_unique<CorrelationTracker>(*exchanges[0], *exchanges[1]);
    }

    // Merged feed from all exchanges
    MergedFeedProcessor merged;
    for (auto& ex : exchanges) {
        ex->tick_signal.connect([&merged](const Tick& t) { merged.on_tick(t); });
    }

    auto start = std::chrono::steady_clock::now();

    switch (mode) {
    case execution_mode::threads:
        run_with_threads(exchanges, processor, *correlation, merged, duration_seconds);
        break;
#if defined(SIGSLOT_HAVE_STDEXEC)
    case execution_mode::stdexec:
        run_with_stdexec(exchanges, processor, *correlation, merged, duration_seconds);
        break;
#endif
    }

    // Final stats
    auto duration = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    double throughput =
        static_cast<double>(processor.raw_tick_count()) / static_cast<double>(ms) * 1000.0;

    std::println("\n{}═══════════════════════════════════════════════════════", color::bold);
    std::println("                   ⚡ HFT RESULTS ⚡                       ");
    std::println("═══════════════════════════════════════════════════════{}", color::reset);

    std::println("Mode:               {}", mode_name);
    std::println("Duration:           {} ms", ms);
    std::println("Total raw ticks:    {}{}{}", color::green, processor.raw_tick_count(),
                 color::reset);
    std::println("Throughput:         {}{:.0f} ticks/sec{}", color::yellow, throughput,
                 color::reset);

    std::println("\n{}Reactive Operators Demonstrated:{}", color::bold, color::reset);
    std::println("  • {}throttle{} - Rate-limited updates to 10ms", color::cyan, color::reset);
    std::println("  • {}distinct{} - Filtered duplicate prices", color::cyan, color::reset);
    std::println("  • {}filter{} - Price threshold filtering", color::cyan, color::reset);
    std::println("  • {}scan{} - Running statistics (mean, volatility)", color::cyan, color::reset);
    std::println("  • {}map{} - Price to cents transformation", color::cyan, color::reset);
    std::println("  • {}combine_latest{} - Stock correlation tracking", color::cyan, color::reset);
    std::println("  • {}merge{} - Combined multi-stock feed", color::cyan, color::reset);

    std::println("\n{}✓ Reactive extensions stress test completed!{}\n", color::green,
                 color::reset);

    return 0;
}
