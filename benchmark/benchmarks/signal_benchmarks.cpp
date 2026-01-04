// Single-threaded benchmarks for sigslot
#include <benchmark/benchmark.h>
#include <sigslot/signal.hpp>

static void BM_SignalConstruction(benchmark::State& state) {
    for (auto _ : state) {
        sigslot::signal<int> sig;
        benchmark::DoNotOptimize(sig);
    }
}

static void BM_SignalDestruction(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto sig = std::make_unique<sigslot::signal<int>>();
        state.ResumeTiming();
        sig.reset();
    }
}

static void BM_ConnectSingleSlot(benchmark::State& state) {
    sigslot::signal<int> sig;
    auto slot = [](int) {};
    
    for (auto _ : state) {
        sig.connect(slot);
        sig.disconnect_all();
    }
}

static void BM_EmissionSingleSlot(benchmark::State& state) {
    sigslot::signal<int> sig;
    sig.connect([](int) {});
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_EmissionMultipleSlots(benchmark::State& state) {
    sigslot::signal<int> sig;
    
    // Connect multiple slots
    for (int i = 0; i < 10; ++i) {
        sig.connect([i](int) {});
    }
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_SlotCount(benchmark::State& state) {
    sigslot::signal<int> sig;
    
    // Connect multiple slots
    for (int i = 0; i < 10; ++i) {
        sig.connect([i](int) {});
    }
    
    for (auto _ : state) {
        auto count = sig.slot_count();
        benchmark::DoNotOptimize(count);
    }
}

BENCHMARK(BM_SignalConstruction);
BENCHMARK(BM_SignalDestruction);
BENCHMARK(BM_ConnectSingleSlot);
BENCHMARK(BM_EmissionSingleSlot);
BENCHMARK(BM_EmissionMultipleSlots);
BENCHMARK(BM_SlotCount);

BENCHMARK_MAIN();
