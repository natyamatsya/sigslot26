// Phase 6.1 Analysis: Measure slot type sizes and vtable overhead
// This analysis informs the slot_variant storage layout design

#include <print>
#include <cstddef>
#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>

#include <sigslot/signal.hpp>

// Forward declarations for size calculation
namespace sigslot::detail {
template<typename, typename...>
class slot_base;
template<typename, typename, typename...>
class slot;
template<typename, typename, typename...>
class slot_extended;
template<typename, typename, typename, typename...>
class slot_pmf;
template<typename, typename, typename, typename...>
class slot_pmf_extended;
template<typename, typename, typename, typename...>
class slot_tracked;
template<typename, typename, typename, typename...>
class slot_pmf_tracked;
} // namespace sigslot::detail

// Test callable types
struct TestClass {
    void method(int) {}
    void method_ext(sigslot::connection&, int) {}
};

using test_lambda = decltype([](int) {});
using test_lambda_capture = decltype([x = 0](int) mutable { x++; });
using test_lambda_ext = decltype([](sigslot::connection&, int) {});

int main() {
    std::println("╔══════════════════════════════════════════════════════════════════╗");
    std::println("║       Phase 6.1: Slot Type Size Analysis for slot_variant        ║");
    std::println("╚══════════════════════════════════════════════════════════════════╝\n");

    // ========================================================================
    // Section 1: Base class sizes (inherited by all slots)
    // ========================================================================
    std::println("┌─────────────────────────────────────────────────────────────────┐");
    std::println("│ 1. Base Class Hierarchy Sizes                                   │");
    std::println("├─────────────────────────────────────────────────────────────────┤");

#ifdef SIGSLOT_USE_INTRUSIVE_PTR
    std::println("│ Mode: INTRUSIVE_PTR (dual-counter)                              │");
    std::println("│   intrusive_refcount:        {:>3} bytes                         │",
                 sizeof(sigslot::detail::intrusive_refcount));
#else
    std::println("│ Mode: std::shared_ptr                                           │");
    std::println("│   enable_shared_from_this:   {:>3} bytes                         │",
                 sizeof(std::enable_shared_from_this<int>));
#endif

    std::println("│   slot_state:                {:>3} bytes                         │",
                 sizeof(sigslot::detail::slot_state));
    std::println("│     - vtable ptr:              8 bytes                          │");
    std::println("│     - m_index (atomic):        8 bytes                          │");
    std::println("│     - m_connected (atomic):    1 byte                           │");
    std::println("│     - m_blocked (atomic):      1 byte                           │");
    std::println("│   grouped_slot<int32_t>:    {:>3} bytes                         │",
                 sizeof(sigslot::detail::grouped_slot<int32_t>));
    std::println("│     - group id:                4 bytes                          │");
    std::println("└─────────────────────────────────────────────────────────────────┘\n");

    // ========================================================================
    // Section 2: Callable storage sizes (what we actually store)
    // ========================================================================
    std::println("┌─────────────────────────────────────────────────────────────────┐");
    std::println("│ 2. Callable Storage Sizes                                       │");
    std::println("├─────────────────────────────────────────────────────────────────┤");
    std::println("│ Type                              Size    Notes                 │");
    std::println("├─────────────────────────────────────────────────────────────────┤");
    std::println("│ void(*)(int)                      {:>3}     Function pointer         │",
                 sizeof(void (*)(int)));
    std::println("│ void(TestClass::*)(int)           {:>3}     PMF (may vary by compiler)│",
                 sizeof(void(TestClass::*)(int)));
    std::println("│ TestClass*                        {:>3}     Object pointer           │",
                 sizeof(TestClass*));
    std::println("│ std::weak_ptr<TestClass>          {:>3}     For tracked slots        │",
                 sizeof(std::weak_ptr<TestClass>));
    std::println("│ std::shared_ptr<TestClass>        {:>3}     For tracked slots        │",
                 sizeof(std::shared_ptr<TestClass>));
    std::println("│ std::function<void(int)>          {:>3}     Type-erased callable     │",
                 sizeof(std::function<void(int)>));
    std::println("│ test_lambda (no capture)          {:>3}     Stateless lambda         │",
                 sizeof(test_lambda));
    std::println("│ test_lambda_capture               {:>3}     Lambda with int capture  │",
                 sizeof(test_lambda_capture));
    std::println("│ sigslot::connection               {:>3}     For extended slots       │",
                 sizeof(sigslot::connection));
    std::println("└─────────────────────────────────────────────────────────────────┘\n");

    // ========================================================================
    // Section 3: Estimated slot_variant storage requirements
    // ========================================================================
    std::println("┌─────────────────────────────────────────────────────────────────┐");
    std::println("│ 3. Estimated slot_variant Storage (without inheritance)         │");
    std::println("├─────────────────────────────────────────────────────────────────┤");

    // Calculate storage needed for each slot type variant
    // slot<G, F, Args...>: just stores std::decay_t<Func>
    size_t plain_lambda = sizeof(test_lambda);          // 1 byte for stateless
    size_t plain_capture = sizeof(test_lambda_capture); // varies

    // slot_pmf<G, Pmf, Ptr, Args...>: stores PMF + Ptr
    size_t pmf_storage = sizeof(void(TestClass::*)(int)) + sizeof(TestClass*);

    // slot_tracked<G, Func, WeakPtr, Args...>: stores Func + weak_ptr
    size_t tracked_storage = sizeof(test_lambda) + sizeof(std::weak_ptr<TestClass>);

    // slot_pmf_tracked: PMF + weak_ptr
    size_t pmf_tracked_storage = sizeof(void(TestClass::*)(int)) + sizeof(std::weak_ptr<TestClass>);

    // slot_extended: Func + connection
    size_t extended_storage = sizeof(test_lambda) + sizeof(sigslot::connection);

    // slot_pmf_extended: PMF + Ptr + connection
    size_t pmf_extended_storage = pmf_storage + sizeof(sigslot::connection);

    std::println("│ Slot Type             Callable Storage   Total Estimate        │");
    std::println("├─────────────────────────────────────────────────────────────────┤");
    std::println("│ plain (stateless λ)        {:>3} bytes       (minimal)              │",
                 plain_lambda);
    std::println("│ plain (capture λ)          {:>3} bytes       (+ capture size)        │",
                 plain_capture);
    std::println("│ pmf                        {:>3} bytes       PMF + object ptr        │",
                 pmf_storage);
    std::println("│ tracked                    {:>3} bytes       λ + weak_ptr            │",
                 tracked_storage);
    std::println("│ pmf_tracked                {:>3} bytes       PMF + weak_ptr          │",
                 pmf_tracked_storage);
    std::println("│ extended                   {:>3} bytes       λ + connection          │",
                 extended_storage);
    std::println("│ pmf_extended               {:>3} bytes       PMF + ptr + connection  │",
                 pmf_extended_storage);
    std::println("└─────────────────────────────────────────────────────────────────┘\n");

    // ========================================================================
    // Section 4: Recommended slot_variant layout
    // ========================================================================

    // Maximum storage needed (conservative estimate)
    size_t max_storage = std::max({plain_capture + 32, // Allow for larger captures
                                   pmf_storage, tracked_storage, pmf_tracked_storage,
                                   extended_storage, pmf_extended_storage});

    // Round up to alignment
    max_storage = (max_storage + 7) & ~7;

    std::println("┌─────────────────────────────────────────────────────────────────┐");
    std::println("│ 4. Recommended slot_variant Layout                              │");
    std::println("├─────────────────────────────────────────────────────────────────┤");
    std::println("│                                                                 │");
    std::println("│ struct slot_variant {{                                           │");
    std::println("│     // Hot data (accessed every emission)                       │");
    std::println("│     call_fn call_;                    //  8 bytes               │");
    std::println("│     atomic<bool> connected_;          //  1 byte                │");
    std::println("│     atomic<bool> blocked_;            //  1 byte                │");
    std::println("│     slot_tag tag_;                    //  1 byte                │");
    std::println("│     uint8_t padding_[5];              //  5 bytes (align)       │");
    std::println("│                                       // ─────────              │");
    std::println("│                                       // 16 bytes hot           │");
    std::println("│                                                                 │");
    std::println("│     // Cold data (accessed on connect/disconnect)               │");
    std::println("│     atomic<size_t> index_;            //  8 bytes               │");
    std::println("│     Group group_;                     //  4 bytes               │");
    std::println("│     uint8_t padding2_[4];             //  4 bytes (align)       │");
    std::println("│                                       // ─────────              │");
    std::println("│                                       // 16 bytes cold          │");
    std::println("│                                                                 │");
    std::println("│     // Variant storage                                          │");
    std::println("│     alignas(8) byte storage_[{:>2}];     // {:>2} bytes max         │",
                 max_storage, max_storage);
    std::println("│                                       // ─────────              │");
    std::println("│     // Total: {:>2} bytes                                       │",
                 32 + max_storage);
    std::println("│ }};                                                              │");
    std::println("│                                                                 │");
    std::println("└─────────────────────────────────────────────────────────────────┘\n");

    // ========================================================================
    // Section 5: Performance impact analysis
    // ========================================================================
    std::println("┌─────────────────────────────────────────────────────────────────┐");
    std::println("│ 5. Performance Impact Analysis                                  │");
    std::println("├─────────────────────────────────────────────────────────────────┤");
    std::println("│                                                                 │");
    std::println("│ Current (virtual dispatch):                                     │");
    std::println("│   1. Load vtable ptr from slot object     ~1 cycle (L1 hit)     │");
    std::println("│   2. Load call_slot ptr from vtable       ~1 cycle (L1 hit)     │");
    std::println("│   3. Indirect call                        ~3-5 cycles           │");
    std::println("│   4. Branch misprediction                 ~15-20 cycles (rare)  │");
    std::println("│   Total: ~5-25 cycles overhead per slot                         │");
    std::println("│                                                                 │");
    std::println("│ Proposed (inline fn ptr):                                       │");
    std::println("│   1. Load call_ fn ptr from slot_variant  ~1 cycle (L1 hit)     │");
    std::println("│   2. Indirect call                        ~3-5 cycles           │");
    std::println("│   Total: ~4-6 cycles overhead per slot                          │");
    std::println("│                                                                 │");
    std::println("│ Expected speedup: ~2-4x for emission hot path                   │");
    std::println("│                                                                 │");
    std::println("│ Additional benefits:                                            │");
    std::println("│   - Better cache locality (no vtable chase)                     │");
    std::println("│   - Smaller object size (no vtable ptr per slot)                │");
    std::println("│   - Potential for inlining with LTO                             │");
    std::println("│                                                                 │");
    std::println("└─────────────────────────────────────────────────────────────────┘\n");

    // ========================================================================
    // Section 6: Design recommendations
    // ========================================================================
    std::println("┌─────────────────────────────────────────────────────────────────┐");
    std::println("│ 6. Design Recommendations                                       │");
    std::println("├─────────────────────────────────────────────────────────────────┤");
    std::println("│                                                                 │");
    std::println("│ ✓ Use inline function pointer for call (eliminates vtable)      │");
    std::println("│ ✓ Use uint8_t tag for slot type (only 7 types needed)           │");
    std::println("│ ✓ Pack hot data together (call_, connected_, blocked_, tag_)    │");
    std::println("│ ✓ Storage size: 48-64 bytes covers most callables               │");
    std::println("│ ✓ Support dynamic fallback for oversized callables              │");
    std::println("│ ✓ Use switch for cold paths (connected check for tracked)       │");
    std::println("│                                                                 │");
    std::println("│ Storage recommendation: 64 bytes                                │");
    std::println("│   - Covers std::function (32 bytes on most platforms)           │");
    std::println("│   - Covers lambdas with moderate captures                       │");
    std::println("│   - Aligns well with cache lines                                │");
    std::println("│                                                                 │");
    std::println("└─────────────────────────────────────────────────────────────────┘");

    return 0;
}
