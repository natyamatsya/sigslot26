// Multi-threaded benchmarks for sigslot
#include <benchmark/benchmark.h>
#include <sigslot/signal.hpp>
#include <thread>
#include <vector>
#include <atomic>

static void BM_ThreadSafeSignalConstruction(benchmark::State& state) {
    for (auto _ : state) {
        sigslot::signal_st<int> sig;
        benchmark::DoNotOptimize(sig);
    }
}

static void BM_ThreadSafeEmission(benchmark::State& state) {
    sigslot::signal_st<int> sig;
    std::atomic<int> sum{0};
    
    // Connect slots
    for (int i = 0; i < 4; ++i) {
        sig.connect([&sum](int x) { sum.fetch_add(x, std::memory_order_relaxed); });
    }
    
    for (auto _ : state) {
        sig(1);
    }
}

static void BM_ConcurrentEmission(benchmark::State& state) {
    sigslot::signal_st<int> sig;
    std::atomic<int> sum{0};
    
    // Connect slots
    for (int i = 0; i < 4; ++i) {
        sig.connect([&sum](int x) { sum.fetch_add(x, std::memory_order_relaxed); });
    }
    
    // Test with different thread counts
    const int num_threads = std::min(4, static_cast<int>(state.range(0)));
    std::vector<std::thread> threads;
    
    for (auto _ : state) {
        threads.clear();
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&sig] {
                for (int j = 0; j < 1000; ++j) {
                    sig(1);
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
}

static void BM_ConcurrentConnect(benchmark::State& state) {
    sigslot::signal_st<int> sig;
    const int num_threads = std::min(4, static_cast<int>(state.range(0)));
    
    for (auto _ : state) {
        std::vector<std::thread> threads;
        
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&sig, i] {
                auto slot = [i](int) {};
                sig.connect(slot);
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        sig.disconnect_all();
    }
}

BENCHMARK(BM_ThreadSafeSignalConstruction);
BENCHMARK(BM_ThreadSafeEmission);
BENCHMARK(BM_ConcurrentEmission)->Arg(1)->Arg(2)->Arg(4);
BENCHMARK(BM_ConcurrentConnect)->Arg(1)->Arg(2)->Arg(4);

BENCHMARK_MAIN();
