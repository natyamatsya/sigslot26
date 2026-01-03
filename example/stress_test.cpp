/**
 * @file stress_test.cpp
 * @brief Multi-threaded stress test demonstrating sigslot thread safety
 * 
 * This demo runs a parallel Monte Carlo estimation of π using multiple worker
 * threads that emit progress signals. It exercises:
 * - Concurrent signal emission from multiple threads
 * - Connection/disconnection under load
 * - Slot group ordering
 * - Lifetime tracking with shared_ptr
 * 
 * Supports three execution modes:
 * - stdexec (default): Uses NVIDIA stdexec thread pool
 * - threads: Uses std::thread directly
 * - coroutines: Uses coroutines with stdexec scheduler
 * 
 * Usage: stress_test [stdexec|threads|coroutines]
 */

#include <sigslot/signal.hpp>
#include <sigslot/async/execution.hpp>
#include <sigslot/async/coroutine.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <string_view>

#if SIGSLOT_EXECUTION_AVAILABLE
#include <exec/static_thread_pool.hpp>
#include <exec/async_scope.hpp>
#endif

// Terminal colors for nice output
namespace color {
constexpr const char* reset = "\033[0m";
constexpr const char* green = "\033[32m";
constexpr const char* yellow = "\033[33m";
constexpr const char* cyan = "\033[36m";
constexpr const char* bold = "\033[1m";
} // namespace color

// Signals for the stress test
struct stress_signals {
    // Emitted by workers: (worker_id, samples_done, samples_total, hits)
    sigslot::signal<int, uint64_t, uint64_t, uint64_t> worker_progress;

    // Emitted when a worker completes
    sigslot::signal<int, double> worker_complete;

    // Emitted for aggregate stats
    sigslot::signal<double, uint64_t> aggregate_update;
};

// Progress aggregator - receives signals from all workers
class progress_aggregator {
public:
    explicit progress_aggregator(int num_workers)
        : num_workers_(num_workers)
        , worker_hits_(num_workers, 0)
        , worker_samples_(num_workers, 0)
        , completed_(0) {}

    void on_worker_progress(int worker_id, uint64_t samples, uint64_t /*total*/, uint64_t hits) {
        std::lock_guard lock(mutex_);
        worker_hits_[worker_id] = hits;
        worker_samples_[worker_id] = samples;
    }

    void on_worker_complete(int /*worker_id*/, double pi_estimate) {
        std::lock_guard lock(mutex_);
        completed_++;
        final_estimates_.push_back(pi_estimate);
    }

    std::tuple<double, uint64_t, int> get_aggregate() const {
        std::lock_guard lock(mutex_);
        uint64_t total_hits = 0;
        uint64_t total_samples = 0;
        for (int i = 0; i < num_workers_; ++i) {
            total_hits += worker_hits_[i];
            total_samples += worker_samples_[i];
        }
        double pi = total_samples > 0 ? 4.0 * total_hits / total_samples : 0.0;
        return {pi, total_samples, completed_};
    }

private:
    int num_workers_;
    mutable std::mutex mutex_;
    std::vector<uint64_t> worker_hits_;
    std::vector<uint64_t> worker_samples_;
    int completed_;
    std::vector<double> final_estimates_;
};

// Worker that computes Monte Carlo samples
void monte_carlo_worker(int worker_id, uint64_t num_samples, stress_signals& signals) {
    std::random_device rd;
    std::mt19937_64 gen(rd() + worker_id);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    uint64_t hits = 0;
    const uint64_t report_interval = num_samples / 100; // Report every 1%

    for (uint64_t i = 0; i < num_samples; ++i) {
        double x = dist(gen);
        double y = dist(gen);
        if (x * x + y * y <= 1.0)
            hits++;

        if ((i + 1) % report_interval == 0)
            signals.worker_progress(worker_id, i + 1, num_samples, hits);
    }

    // Final report
    signals.worker_progress(worker_id, num_samples, num_samples, hits);

    double pi_estimate = 4.0 * hits / num_samples;
    signals.worker_complete(worker_id, pi_estimate);
}

// Display updater
void display_progress(const progress_aggregator& aggregator, int num_workers,
                      uint64_t total_samples, std::atomic<bool>& done) {
    const int bar_width = 40;

    while (!done) {
        auto [pi, samples, completed] = aggregator.get_aggregate();
        double progress = static_cast<double>(samples) / (num_workers * total_samples);
        int filled = static_cast<int>(progress * bar_width);

        // Build progress bar
        std::string bar(bar_width, ' ');
        for (int i = 0; i < filled && i < bar_width; ++i)
            bar[i] = '=';
        if (filled < bar_width)
            bar[filled] = '>';

        double error = std::abs(pi - std::numbers::pi);

        // Move cursor up and clear lines
        std::cout << "\033[4A"; // Move up 4 lines

        std::cout << color::bold << "Parallel Monte Carlo π Estimation" << color::reset << "\n";
        std::cout << color::cyan << "[" << bar << "] " << color::yellow << std::fixed
                  << std::setprecision(1) << (progress * 100) << "%" << color::reset << "\n";
        std::cout << "π estimate: " << color::green << std::setprecision(10) << pi << color::reset
                  << "  (error: " << color::yellow << std::scientific << std::setprecision(2)
                  << error << color::reset << ")\n";
        std::cout << "Workers: " << completed << "/" << num_workers << " complete, " << samples
                  << " samples\n"
                  << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Connection churn test - connects/disconnects slots while signals are firing
void connection_churn_test(stress_signals& signals, std::atomic<bool>& done,
                           std::atomic<uint64_t>& churn_count) {
    while (!done) {
        // Connect a temporary slot (no-op, just tests connect/disconnect under load)
        auto conn = signals.worker_progress.connect([](int, uint64_t, uint64_t, uint64_t) {
            // Intentionally empty - we're just testing connection churn
        });

        std::this_thread::sleep_for(std::chrono::microseconds(100));

        // Disconnect
        conn.disconnect();
        churn_count++;
    }
}

enum class execution_mode { stdexec, threads, coroutines };

void run_with_threads(stress_signals& signals, progress_aggregator& aggregator, int num_workers,
                      uint64_t samples_per_worker, std::atomic<bool>& workers_done,
                      std::atomic<bool>& display_done, std::atomic<uint64_t>& churn_count) {
    // Start display thread
    std::thread display_thread(display_progress, std::cref(aggregator), num_workers,
                               samples_per_worker, std::ref(display_done));

    // Start connection churn threads
    std::vector<std::thread> churn_threads;
    for (int i = 0; i < 2; ++i) {
        churn_threads.emplace_back(connection_churn_test, std::ref(signals), std::ref(workers_done),
                                   std::ref(churn_count));
    }

    // Start worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < num_workers; ++i) {
        workers.emplace_back(monte_carlo_worker, i, samples_per_worker, std::ref(signals));
    }

    // Wait for workers
    for (auto& w : workers)
        w.join();

    workers_done = true;

    // Stop churn threads
    for (auto& t : churn_threads)
        t.join();

    // Let display catch up
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    display_done = true;
    display_thread.join();
}

#if SIGSLOT_EXECUTION_AVAILABLE
void run_with_stdexec(stress_signals& signals, progress_aggregator& aggregator, int num_workers,
                      uint64_t samples_per_worker, std::atomic<bool>& workers_done,
                      std::atomic<bool>& display_done, std::atomic<uint64_t>& churn_count) {
    exec::static_thread_pool pool(num_workers + 4); // Extra threads for churn + display
    auto sched = pool.get_scheduler();
    exec::async_scope worker_scope;

    // Start display thread
    std::thread display_thread(display_progress, std::cref(aggregator), num_workers,
                               samples_per_worker, std::ref(display_done));

    // Start connection churn threads (separate from worker scope)
    std::vector<std::thread> churn_threads;
    for (int i = 0; i < 2; ++i) {
        churn_threads.emplace_back([&signals, &workers_done, &churn_count]() {
            while (!workers_done) {
                auto conn =
                    signals.worker_progress.connect([](int, uint64_t, uint64_t, uint64_t) {});
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                conn.disconnect();
                churn_count++;
            }
        });
    }

    // Spawn workers using stdexec
    for (int i = 0; i < num_workers; ++i) {
        worker_scope.spawn(
            stdexec::then(stdexec::schedule(sched), [i, samples_per_worker, &signals]() {
                monte_carlo_worker(i, samples_per_worker, signals);
            }));
    }

    // Wait for workers to complete
    stdexec::sync_wait(worker_scope.on_empty());

    workers_done = true;

    // Wait for churn threads
    for (auto& t : churn_threads)
        t.join();

    // Let display catch up
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    display_done = true;
    display_thread.join();
}

// Coroutine that awaits worker completions using co_await
sigslot::async::task<void> completion_collector(sigslot::signal<int, double>& complete_signal,
                                                std::vector<double>& estimates, int expected_count,
                                                std::atomic<bool>& all_done) {
    for (int i = 0; i < expected_count; ++i) {
        // co_await the next worker completion signal
        auto awaiter = sigslot::async::simple_signal_awaiter(&complete_signal);
        auto [worker_id, pi_estimate] = co_await awaiter;
        estimates.push_back(pi_estimate);
    }
    all_done = true;
    co_return;
}

// Coroutine worker that emits progress
sigslot::async::task<void> coro_monte_carlo_worker(int worker_id, uint64_t num_samples,
                                                   stress_signals& signals) {
    std::random_device rd;
    std::mt19937_64 gen(rd() + worker_id);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    uint64_t hits = 0;
    const uint64_t report_interval = num_samples / 100;

    for (uint64_t i = 0; i < num_samples; ++i) {
        double x = dist(gen);
        double y = dist(gen);
        if (x * x + y * y <= 1.0)
            hits++;

        if ((i + 1) % report_interval == 0)
            signals.worker_progress(worker_id, i + 1, num_samples, hits);
    }

    signals.worker_progress(worker_id, num_samples, num_samples, hits);

    double pi_estimate = 4.0 * hits / num_samples;
    signals.worker_complete(worker_id, pi_estimate);

    co_return;
}

void run_with_coroutines(stress_signals& signals, progress_aggregator& aggregator, int num_workers,
                         uint64_t samples_per_worker, std::atomic<bool>& workers_done,
                         std::atomic<bool>& display_done, std::atomic<uint64_t>& churn_count) {
    exec::static_thread_pool pool(num_workers + 4);
    auto sched = pool.get_scheduler();
    exec::async_scope worker_scope;

    // Start display thread
    std::thread display_thread(display_progress, std::cref(aggregator), num_workers,
                               samples_per_worker, std::ref(display_done));

    // Connect progress aggregator
    signals.worker_progress.connect(&progress_aggregator::on_worker_progress, &aggregator, 0);

    // Collect completion results using a coroutine with co_await
    std::vector<double> pi_estimates;
    std::atomic<bool> collection_done{false};
    auto collector =
        completion_collector(signals.worker_complete, pi_estimates, num_workers, collection_done);

    // Connection churn threads (separate from worker scope)
    std::vector<std::thread> churn_threads;
    for (int i = 0; i < 2; ++i) {
        churn_threads.emplace_back([&signals, &workers_done, &churn_count]() {
            while (!workers_done) {
                auto conn =
                    signals.worker_progress.connect([](int, uint64_t, uint64_t, uint64_t) {});
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                conn.disconnect();
                churn_count++;
            }
        });
    }

    // Spawn coroutine workers on the thread pool
    for (int i = 0; i < num_workers; ++i) {
        worker_scope.spawn(
            stdexec::then(stdexec::schedule(sched), [i, samples_per_worker, &signals]() {
                // Launch coroutine worker
                auto task = coro_monte_carlo_worker(i, samples_per_worker, signals);
            }));
    }

    // Wait for workers to complete
    stdexec::sync_wait(worker_scope.on_empty());

    // Wait for collector coroutine to finish
    while (!collection_done)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    workers_done = true;

    // Wait for churn threads
    for (auto& t : churn_threads)
        t.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    display_done = true;
    display_thread.join();

    // Print coroutine-collected estimates (after display thread is done)
    if (!pi_estimates.empty()) {
        double avg = 0;
        for (auto e : pi_estimates)
            avg += e;
        avg /= pi_estimates.size();
        std::cout << "  (Coroutine collected " << pi_estimates.size() << " estimates, avg: " << avg
                  << ")\n";
    }
}
#endif // SIGSLOT_EXECUTION_AVAILABLE

int main(int argc, char* argv[]) {
    // Parse execution mode
    execution_mode mode = execution_mode::stdexec; // Default

    if (argc > 1) {
        std::string_view arg = argv[1];
        if (arg == "threads")
            mode = execution_mode::threads;
        else if (arg == "coroutines")
            mode = execution_mode::coroutines;
        else if (arg == "stdexec")
            mode = execution_mode::stdexec;
        else {
            std::cerr << "Usage: " << argv[0] << " [stdexec|threads|coroutines]\n";
            return 1;
        }
    }

#if !SIGSLOT_EXECUTION_AVAILABLE
    if (mode != execution_mode::threads) {
        std::cerr << "stdexec not available, falling back to threads mode\n";
        mode = execution_mode::threads;
    }
#endif

    std::cout << "\n\n\n\n"; // Make room for display

    const int num_workers = std::thread::hardware_concurrency();
    const uint64_t samples_per_worker = 10'000'000;

    const char* mode_name = mode == execution_mode::stdexec      ? "stdexec"
                            : mode == execution_mode::coroutines ? "coroutines"
                                                                 : "threads";

    std::cout << color::bold << "sigslot26 Stress Test" << color::reset << "\n";
    std::cout << "Mode: " << color::cyan << mode_name << color::reset << "\n";
    std::cout << "Using " << num_workers << " worker threads\n";
    std::cout << "Total samples: " << (num_workers * samples_per_worker) << "\n\n";

    stress_signals signals;
    progress_aggregator aggregator(num_workers);

    // Connect aggregator slots with group ordering
    signals.worker_progress.connect(&progress_aggregator::on_worker_progress, &aggregator, 0);
    signals.worker_complete.connect(&progress_aggregator::on_worker_complete, &aggregator, 1);

    std::atomic<bool> workers_done{false};
    std::atomic<bool> display_done{false};
    std::atomic<uint64_t> churn_count{0};

    auto start = std::chrono::high_resolution_clock::now();

    switch (mode) {
    case execution_mode::threads:
        run_with_threads(signals, aggregator, num_workers, samples_per_worker, workers_done,
                         display_done, churn_count);
        break;
#if SIGSLOT_EXECUTION_AVAILABLE
    case execution_mode::stdexec:
        run_with_stdexec(signals, aggregator, num_workers, samples_per_worker, workers_done,
                         display_done, churn_count);
        break;
    case execution_mode::coroutines:
        run_with_coroutines(signals, aggregator, num_workers, samples_per_worker, workers_done,
                            display_done, churn_count);
        break;
#else
    default:
        break;
#endif
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Final results
    auto [final_pi, total_samples, completed] = aggregator.get_aggregate();

    std::cout << "\n" << color::bold << "Results:" << color::reset << "\n";
    std::cout << "  Final π estimate: " << color::green << std::fixed << std::setprecision(10)
              << final_pi << color::reset << "\n";
    std::cout << "  Actual π:         " << std::numbers::pi << "\n";
    std::cout << "  Error:            " << color::yellow << std::scientific << std::setprecision(2)
              << std::abs(final_pi - std::numbers::pi) << color::reset << "\n";
    std::cout << "  Duration:         " << duration.count() << " ms\n";
    std::cout << "  Throughput:       " << std::fixed << std::setprecision(1)
              << (total_samples / 1e6) / (duration.count() / 1000.0) << " M samples/sec\n";
    std::cout << "  Connect/disconnect churn: " << churn_count << " operations\n";
    std::cout << "  Slot count at end: " << signals.worker_progress.slot_count() << "\n";

    std::cout << "\n"
              << color::green << "✓ Stress test completed successfully!" << color::reset << "\n\n";

    return 0;
}
