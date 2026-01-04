// Phase 6.5: Benchmarks comparing slot_variant vs virtual dispatch
#include <benchmark/benchmark.h>
#include <sigslot/signal.hpp>
#include <sigslot/slot-variant.hpp>
#include <sigslot/signal-inline.hpp>
#include <vector>
#include <memory>

// ============================================================================
// Baseline: Current virtual dispatch implementation
// ============================================================================

static void BM_VirtualDispatch_SingleSlot(benchmark::State& state) {
    sigslot::signal<int> sig;
    sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_VirtualDispatch_10Slots(benchmark::State& state) {
    sigslot::signal<int> sig;
    for (int i = 0; i < 10; ++i) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    }
    
    for (auto _ : state) {
        sig(42);
    }
}

// ============================================================================
// New: slot_variant inline function pointer dispatch
// ============================================================================

static void BM_SlotVariant_SingleSlot(benchmark::State& state) {
    using slot_t = sigslot::detail::slot_variant<int32_t, int>;
    
    auto slot = slot_t::make_plain(
        [](int x) { benchmark::DoNotOptimize(x); },
        int32_t{0}
    );
    
    for (auto _ : state) {
        slot(42);
    }
}

static void BM_SlotVariant_10Slots(benchmark::State& state) {
    using slot_t = sigslot::detail::slot_variant<int32_t, int>;
    
    std::vector<slot_t> slots;
    slots.reserve(10);
    for (int i = 0; i < 10; ++i) {
        slots.push_back(slot_t::make_plain(
            [](int x) { benchmark::DoNotOptimize(x); },
            int32_t{0}
        ));
    }
    
    for (auto _ : state) {
        for (auto& slot : slots) {
            slot(42);
        }
    }
}

// ============================================================================
// Construction benchmarks
// ============================================================================

static void BM_VirtualDispatch_Connect(benchmark::State& state) {
    sigslot::signal<int> sig;
    
    for (auto _ : state) {
        auto conn = sig.connect([](int x) { benchmark::DoNotOptimize(x); });
        sig.disconnect_all();
    }
}

static void BM_SlotVariant_Construct(benchmark::State& state) {
    using slot_t = sigslot::detail::slot_variant<int32_t, int>;
    
    for (auto _ : state) {
        auto slot = slot_t::make_plain(
            [](int x) { benchmark::DoNotOptimize(x); },
            int32_t{0}
        );
        benchmark::DoNotOptimize(slot);
    }
}

// ============================================================================
// PMF (pointer-to-member-function) benchmarks
// ============================================================================

struct TestReceiver {
    int value = 0;
    void on_signal(int x) { value = x; }
};

static void BM_VirtualDispatch_PMF(benchmark::State& state) {
    sigslot::signal<int> sig;
    TestReceiver receiver;
    sig.connect(&TestReceiver::on_signal, &receiver);
    
    for (auto _ : state) {
        sig(42);
        benchmark::DoNotOptimize(receiver.value);
    }
}

static void BM_SlotVariant_PMF(benchmark::State& state) {
    using slot_t = sigslot::detail::slot_variant<int32_t, int>;
    
    TestReceiver receiver;
    auto slot = slot_t::make_pmf(&TestReceiver::on_signal, &receiver, int32_t{0});
    
    for (auto _ : state) {
        slot(42);
        benchmark::DoNotOptimize(receiver.value);
    }
}

// ============================================================================
// Tracked slot benchmarks (with weak_ptr lifetime management)
// ============================================================================

static void BM_VirtualDispatch_Tracked(benchmark::State& state) {
    sigslot::signal<int> sig;
    auto receiver = std::make_shared<TestReceiver>();
    sig.connect(&TestReceiver::on_signal, receiver);
    
    for (auto _ : state) {
        sig(42);
        benchmark::DoNotOptimize(receiver->value);
    }
}

static void BM_SlotVariant_Tracked(benchmark::State& state) {
    using slot_t = sigslot::detail::slot_variant<int32_t, int>;
    
    auto receiver = std::make_shared<TestReceiver>();
    std::weak_ptr<TestReceiver> weak = receiver;
    
    auto slot = slot_t::make_pmf_tracked(&TestReceiver::on_signal, weak, int32_t{0});
    
    for (auto _ : state) {
        slot(42);
        benchmark::DoNotOptimize(receiver->value);
    }
}

// ============================================================================
// Memory layout analysis
// ============================================================================

static void BM_SlotVariant_SizeOf(benchmark::State& state) {
    using slot_t = sigslot::detail::slot_variant<int32_t, int>;
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(sizeof(slot_t));
    }
    
    state.counters["sizeof_slot_variant"] = sizeof(slot_t);
    state.counters["storage_size"] = slot_t::storage_size;
}

// ============================================================================
// Cache locality stress test - many slots
// ============================================================================

static void BM_VirtualDispatch_100Slots(benchmark::State& state) {
    sigslot::signal<int> sig;
    for (int i = 0; i < 100; ++i) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    }
    
    for (auto _ : state) {
        sig(42);
    }
    state.SetItemsProcessed(state.iterations() * 100);
}

static void BM_SlotVariant_100Slots_Inline(benchmark::State& state) {
    using slot_t = sigslot::detail::slot_variant<int32_t, int>;
    
    // Inline storage - contiguous in memory
    std::vector<slot_t> slots;
    slots.reserve(100);
    for (int i = 0; i < 100; ++i) {
        slots.push_back(slot_t::make_plain(
            [](int x) { benchmark::DoNotOptimize(x); },
            int32_t{0}
        ));
    }
    
    for (auto _ : state) {
        for (auto& slot : slots) {
            slot(42);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
}

static void BM_SlotVariant_100Slots_Pointer(benchmark::State& state) {
    using slot_t = sigslot::detail::slot_variant<int32_t, int>;
    
    // Pointer-based storage - scattered in memory (simulates current impl)
    std::vector<std::unique_ptr<slot_t>> slots;
    slots.reserve(100);
    for (int i = 0; i < 100; ++i) {
        slots.push_back(std::make_unique<slot_t>(slot_t::make_plain(
            [](int x) { benchmark::DoNotOptimize(x); },
            int32_t{0}
        )));
    }
    
    for (auto _ : state) {
        for (auto& slot : slots) {
            (*slot)(42);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
}

// ============================================================================
// signal_inline benchmarks - full signal with inline storage
// ============================================================================

static void BM_SignalInline_SingleSlot(benchmark::State& state) {
    sigslot::signal_inline<int> sig;
    sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_SignalInline_10Slots(benchmark::State& state) {
    sigslot::signal_inline<int> sig;
    for (int i = 0; i < 10; ++i) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    }
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_SignalInline_100Slots(benchmark::State& state) {
    sigslot::signal_inline<int> sig;
    sig.reserve(100);
    for (int i = 0; i < 100; ++i) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    }
    
    for (auto _ : state) {
        sig(42);
    }
    state.SetItemsProcessed(state.iterations() * 100);
}

static void BM_SignalInline_Connect(benchmark::State& state) {
    sigslot::signal_inline<int> sig;
    
    for (auto _ : state) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
        sig.disconnect_all();
    }
}

static void BM_SignalInline_PMF(benchmark::State& state) {
    sigslot::signal_inline<int> sig;
    TestReceiver receiver;
    sig.connect(&TestReceiver::on_signal, &receiver);
    
    for (auto _ : state) {
        sig(42);
        benchmark::DoNotOptimize(receiver.value);
    }
}

// ============================================================================
// Thread-safe signal_inline benchmarks
// ============================================================================

static void BM_SignalInlineRW_SingleSlot(benchmark::State& state) {
    sigslot::signal_inline_rw<int> sig;
    sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_SignalInlineRW_10Slots(benchmark::State& state) {
    sigslot::signal_inline_rw<int> sig;
    for (int i = 0; i < 10; ++i) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    }
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_SignalInlineRCU_SingleSlot(benchmark::State& state) {
    sigslot::signal_inline_rcu<int> sig;
    sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_SignalInlineRCU_10Slots(benchmark::State& state) {
    sigslot::signal_inline_rcu<int> sig;
    for (int i = 0; i < 10; ++i) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    }
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_SignalInlineSeqlock_SingleSlot(benchmark::State& state) {
    sigslot::signal_inline_seqlock<int> sig;
    sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    
    for (auto _ : state) {
        sig(42);
    }
}

static void BM_SignalInlineSeqlock_10Slots(benchmark::State& state) {
    sigslot::signal_inline_seqlock<int> sig;
    for (int i = 0; i < 10; ++i) {
        sig.connect([](int x) { benchmark::DoNotOptimize(x); });
    }
    
    for (auto _ : state) {
        sig(42);
    }
}

// ============================================================================
// Register benchmarks
// ============================================================================

// Emission benchmarks - compare all three approaches
BENCHMARK(BM_VirtualDispatch_SingleSlot);
BENCHMARK(BM_SlotVariant_SingleSlot);
BENCHMARK(BM_SignalInline_SingleSlot);

BENCHMARK(BM_VirtualDispatch_10Slots);
BENCHMARK(BM_SlotVariant_10Slots);
BENCHMARK(BM_SignalInline_10Slots);

BENCHMARK(BM_VirtualDispatch_100Slots);
BENCHMARK(BM_SlotVariant_100Slots_Inline);
BENCHMARK(BM_SlotVariant_100Slots_Pointer);
BENCHMARK(BM_SignalInline_100Slots);

// Construction benchmarks
BENCHMARK(BM_VirtualDispatch_Connect);
BENCHMARK(BM_SlotVariant_Construct);
BENCHMARK(BM_SignalInline_Connect);

// PMF benchmarks
BENCHMARK(BM_VirtualDispatch_PMF);
BENCHMARK(BM_SlotVariant_PMF);
BENCHMARK(BM_SignalInline_PMF);

// Tracked benchmarks
BENCHMARK(BM_VirtualDispatch_Tracked);
BENCHMARK(BM_SlotVariant_Tracked);

// Thread-safe variants
BENCHMARK(BM_SignalInlineRW_SingleSlot);
BENCHMARK(BM_SignalInlineRW_10Slots);
BENCHMARK(BM_SignalInlineRCU_SingleSlot);
BENCHMARK(BM_SignalInlineRCU_10Slots);
BENCHMARK(BM_SignalInlineSeqlock_SingleSlot);
BENCHMARK(BM_SignalInlineSeqlock_10Slots);

// Size analysis
BENCHMARK(BM_SlotVariant_SizeOf);

BENCHMARK_MAIN();
