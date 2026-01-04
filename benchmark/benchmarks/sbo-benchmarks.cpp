// SBO-specific benchmarks
#include <benchmark/benchmark.h>
#include <sigslot/signal-sbo.hpp>
#include <sigslot/signal.hpp>

// Simple SBO signal test for now (we'll integrate with real signal later)
template<std::size_t SBO_SIZE>
class test_sbo_signal {
    sigslot::detail::sbo_container<int, SBO_SIZE> slots_;
    
public:
    template<typename F>
    void connect(F&& f) {
        slots_.emplace_back(std::forward<F>(f));
    }
    
    void operator()(int value) {
        for (auto& slot : slots_.get_span()) {
            slot(value);
        }
    }
    
    std::size_t slot_count() const { return slots_.size(); }
    void disconnect_all() { slots_.clear(); }
};

static void BM_SBO_ContainerConstruction_Empty(benchmark::State& state) {
    for (auto _ : state) {
        sigslot::detail::sbo_container<int, 3> sbo;
        benchmark::DoNotOptimize(sbo);
    }
}

static void BM_SBO_ContainerConstruction_OneSlot(benchmark::State& state) {
    for (auto _ : state) {
        sigslot::detail::sbo_container<int, 3> sbo;
        sbo.emplace_back(42);
        benchmark::DoNotOptimize(sbo);
    }
}

static void BM_SBO_ContainerConstruction_ThreeSlots(benchmark::State& state) {
    for (auto _ : state) {
        sigslot::detail::sbo_container<int, 3> sbo;
        sbo.emplace_back(1);
        sbo.emplace_back(2);
        sbo.emplace_back(3);
        benchmark::DoNotOptimize(sbo);
    }
}

static void BM_SBO_ContainerConstruction_FiveSlots(benchmark::State& state) {
    for (auto _ : state) {
        sigslot::detail::sbo_container<int, 3> sbo;
        sbo.emplace_back(1);
        sbo.emplace_back(2);
        sbo.emplace_back(3);
        sbo.emplace_back(4);
        sbo.emplace_back(5);
        benchmark::DoNotOptimize(sbo);
    }
}

static void BM_SBO_EmplaceBack_Stack(benchmark::State& state) {
    sigslot::detail::sbo_container<int, 3> sbo;
    
    for (auto _ : state) {
        state.PauseTiming();
        if (sbo.size() >= 3) {
            sbo.clear();
        }
        state.ResumeTiming();
        
        sbo.emplace_back(42);
    }
}

static void BM_SBO_EmplaceBack_Heap(benchmark::State& state) {
    sigslot::detail::sbo_container<int, 2> sbo;
    
    for (auto _ : state) {
        state.PauseTiming();
        if (sbo.size() == 0) {
            // Pre-fill to force heap
            sbo.emplace_back(1);
            sbo.emplace_back(2);
        }
        state.ResumeTiming();
        
        sbo.emplace_back(42);
    }
}

static void BM_SBO_Iteration_Stack(benchmark::State& state) {
    sigslot::detail::sbo_container<int, 3> sbo;
    sbo.emplace_back(1);
    sbo.emplace_back(2);
    sbo.emplace_back(3);
    
    for (auto _ : state) {
        int sum = 0;
        for (auto& val : sbo.get_span()) {
            sum += val;
        }
        benchmark::DoNotOptimize(sum);
    }
}

static void BM_SBO_Iteration_Heap(benchmark::State& state) {
    sigslot::detail::sbo_container<int, 2> sbo;
    sbo.emplace_back(1);
    sbo.emplace_back(2);
    sbo.emplace_back(3);
    sbo.emplace_back(4);
    sbo.emplace_back(5);
    
    for (auto _ : state) {
        int sum = 0;
        for (auto& val : sbo.get_span()) {
            sum += val;
        }
        benchmark::DoNotOptimize(sum);
    }
}

// Comparison with regular signal
static void BM_Regular_SignalConstruction(benchmark::State& state) {
    for (auto _ : state) {
        sigslot::signal<int> sig;
        benchmark::DoNotOptimize(sig);
    }
}

static void BM_Regular_Connect(benchmark::State& state) {
    sigslot::signal<int> sig;
    
    for (auto _ : state) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
        sig.disconnect_all();
    }
}

static void BM_Regular_Emission(benchmark::State& state) {
    sigslot::signal<int> sig;
    
    for (int i = 0; i < 3; ++i) {
        sig.connect([i](int x) { benchmark::DoNotOptimize(x); });
    }
    
    for (auto _ : state) {
        sig(42);
    }
}

// Register benchmarks
BENCHMARK(BM_SBO_ContainerConstruction_Empty);
BENCHMARK(BM_SBO_ContainerConstruction_OneSlot);
BENCHMARK(BM_SBO_ContainerConstruction_ThreeSlots);
BENCHMARK(BM_SBO_ContainerConstruction_FiveSlots);
BENCHMARK(BM_SBO_EmplaceBack_Stack);
BENCHMARK(BM_SBO_EmplaceBack_Heap);
BENCHMARK(BM_SBO_Iteration_Stack);
BENCHMARK(BM_SBO_Iteration_Heap);

// Comparison benchmarks
BENCHMARK(BM_Regular_SignalConstruction);
BENCHMARK(BM_Regular_Connect);
BENCHMARK(BM_Regular_Emission);

BENCHMARK_MAIN();
