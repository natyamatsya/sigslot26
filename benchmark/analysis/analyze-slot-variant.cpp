// Phase 6.1 Analysis: Measure slot type sizes and vtable overhead
// This analysis informs the slot_variant storage layout design

#include <iostream>
#include <iomanip>
#include <cstddef>
#include <atomic>
#include <functional>
#include <memory>
#include <type_traits>

#include <sigslot/signal.hpp>

// Forward declarations for size calculation
namespace sigslot::detail {
    template<typename, typename...> class slot_base;
    template<typename, typename, typename...> class slot;
    template<typename, typename, typename...> class slot_extended;
    template<typename, typename, typename, typename...> class slot_pmf;
    template<typename, typename, typename, typename...> class slot_pmf_extended;
    template<typename, typename, typename, typename...> class slot_tracked;
    template<typename, typename, typename, typename...> class slot_pmf_tracked;
}

// Test callable types
struct TestClass {
    void method(int) {}
    void method_ext(sigslot::connection&, int) {}
};

using test_lambda = decltype([](int){});
using test_lambda_capture = decltype([x = 0](int) mutable { x++; });
using test_lambda_ext = decltype([](sigslot::connection&, int){});

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       Phase 6.1: Slot Type Size Analysis for slot_variant        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";

    // ========================================================================
    // Section 1: Base class sizes (inherited by all slots)
    // ========================================================================
    std::cout << "┌─────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ 1. Base Class Hierarchy Sizes                                   │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
    std::cout << "│ Mode: INTRUSIVE_PTR (dual-counter)                              │\n";
    std::cout << "│   intrusive_refcount:        " << std::setw(3) << sizeof(sigslot::detail::intrusive_refcount) << " bytes                         │\n";
#else
    std::cout << "│ Mode: std::shared_ptr                                           │\n";
    std::cout << "│   enable_shared_from_this:   " << std::setw(3) << sizeof(std::enable_shared_from_this<int>) << " bytes                         │\n";
#endif
    
    std::cout << "│   slot_state:                " << std::setw(3) << sizeof(sigslot::detail::slot_state) << " bytes                         │\n";
    std::cout << "│     - vtable ptr:              8 bytes                          │\n";
    std::cout << "│     - m_index (atomic):        8 bytes                          │\n";
    std::cout << "│     - m_connected (atomic):    1 byte                           │\n";
    std::cout << "│     - m_blocked (atomic):      1 byte                           │\n";
    std::cout << "│   grouped_slot<int32_t>:    " << std::setw(3) << sizeof(sigslot::detail::grouped_slot<int32_t>) << " bytes                         │\n";
    std::cout << "│     - group id:                4 bytes                          │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────┘\n\n";

    // ========================================================================
    // Section 2: Callable storage sizes (what we actually store)
    // ========================================================================
    std::cout << "┌─────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ 2. Callable Storage Sizes                                       │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Type                              Size    Notes                 │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ void(*)(int)                      " << std::setw(3) << sizeof(void(*)(int)) << "     Function pointer         │\n";
    std::cout << "│ void(TestClass::*)(int)           " << std::setw(3) << sizeof(void(TestClass::*)(int)) << "     PMF (may vary by compiler)│\n";
    std::cout << "│ TestClass*                        " << std::setw(3) << sizeof(TestClass*) << "     Object pointer           │\n";
    std::cout << "│ std::weak_ptr<TestClass>          " << std::setw(3) << sizeof(std::weak_ptr<TestClass>) << "     For tracked slots        │\n";
    std::cout << "│ std::shared_ptr<TestClass>        " << std::setw(3) << sizeof(std::shared_ptr<TestClass>) << "     For tracked slots        │\n";
    std::cout << "│ std::function<void(int)>          " << std::setw(3) << sizeof(std::function<void(int)>) << "     Type-erased callable     │\n";
    std::cout << "│ test_lambda (no capture)          " << std::setw(3) << sizeof(test_lambda) << "     Stateless lambda         │\n";
    std::cout << "│ test_lambda_capture               " << std::setw(3) << sizeof(test_lambda_capture) << "     Lambda with int capture  │\n";
    std::cout << "│ sigslot::connection               " << std::setw(3) << sizeof(sigslot::connection) << "     For extended slots       │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────┘\n\n";

    // ========================================================================
    // Section 3: Estimated slot_variant storage requirements
    // ========================================================================
    std::cout << "┌─────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ 3. Estimated slot_variant Storage (without inheritance)         │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    
    // Calculate storage needed for each slot type variant
    // slot<G, F, Args...>: just stores std::decay_t<Func>
    size_t plain_lambda = sizeof(test_lambda);  // 1 byte for stateless
    size_t plain_capture = sizeof(test_lambda_capture);  // varies
    
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
    
    std::cout << "│ Slot Type             Callable Storage   Total Estimate        │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ plain (stateless λ)        " << std::setw(3) << plain_lambda << " bytes       (minimal)              │\n";
    std::cout << "│ plain (capture λ)          " << std::setw(3) << plain_capture << " bytes       (+ capture size)        │\n";
    std::cout << "│ pmf                        " << std::setw(3) << pmf_storage << " bytes       PMF + object ptr        │\n";
    std::cout << "│ tracked                    " << std::setw(3) << tracked_storage << " bytes       λ + weak_ptr            │\n";
    std::cout << "│ pmf_tracked                " << std::setw(3) << pmf_tracked_storage << " bytes       PMF + weak_ptr          │\n";
    std::cout << "│ extended                   " << std::setw(3) << extended_storage << " bytes       λ + connection          │\n";
    std::cout << "│ pmf_extended               " << std::setw(3) << pmf_extended_storage << " bytes       PMF + ptr + connection  │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────┘\n\n";

    // ========================================================================
    // Section 4: Recommended slot_variant layout
    // ========================================================================
    
    // Maximum storage needed (conservative estimate)
    size_t max_storage = std::max({
        plain_capture + 32,     // Allow for larger captures
        pmf_storage,
        tracked_storage,
        pmf_tracked_storage,
        extended_storage,
        pmf_extended_storage
    });
    
    // Round up to alignment
    max_storage = (max_storage + 7) & ~7;
    
    std::cout << "┌─────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ 4. Recommended slot_variant Layout                              │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│ struct slot_variant {                                           │\n";
    std::cout << "│     // Hot data (accessed every emission)                       │\n";
    std::cout << "│     call_fn call_;                    //  8 bytes               │\n";
    std::cout << "│     atomic<bool> connected_;          //  1 byte                │\n";
    std::cout << "│     atomic<bool> blocked_;            //  1 byte                │\n";
    std::cout << "│     slot_tag tag_;                    //  1 byte                │\n";
    std::cout << "│     uint8_t padding_[5];              //  5 bytes (align)       │\n";
    std::cout << "│                                       // ─────────              │\n";
    std::cout << "│                                       // 16 bytes hot           │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│     // Cold data (accessed on connect/disconnect)               │\n";
    std::cout << "│     atomic<size_t> index_;            //  8 bytes               │\n";
    std::cout << "│     Group group_;                     //  4 bytes               │\n";
    std::cout << "│     uint8_t padding2_[4];             //  4 bytes (align)       │\n";
    std::cout << "│                                       // ─────────              │\n";
    std::cout << "│                                       // 16 bytes cold          │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│     // Variant storage                                          │\n";
    std::cout << "│     alignas(8) byte storage_[" << std::setw(2) << max_storage << "];     // " << std::setw(2) << max_storage << " bytes max         │\n";
    std::cout << "│                                       // ─────────              │\n";
    std::cout << "│     // Total: " << std::setw(2) << (32 + max_storage) << " bytes                                       │\n";
    std::cout << "│ };                                                              │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────┘\n\n";

    // ========================================================================
    // Section 5: Performance impact analysis
    // ========================================================================
    std::cout << "┌─────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ 5. Performance Impact Analysis                                  │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│ Current (virtual dispatch):                                     │\n";
    std::cout << "│   1. Load vtable ptr from slot object     ~1 cycle (L1 hit)     │\n";
    std::cout << "│   2. Load call_slot ptr from vtable       ~1 cycle (L1 hit)     │\n";
    std::cout << "│   3. Indirect call                        ~3-5 cycles           │\n";
    std::cout << "│   4. Branch misprediction                 ~15-20 cycles (rare)  │\n";
    std::cout << "│   Total: ~5-25 cycles overhead per slot                         │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│ Proposed (inline fn ptr):                                       │\n";
    std::cout << "│   1. Load call_ fn ptr from slot_variant  ~1 cycle (L1 hit)     │\n";
    std::cout << "│   2. Indirect call                        ~3-5 cycles           │\n";
    std::cout << "│   Total: ~4-6 cycles overhead per slot                          │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│ Expected speedup: ~2-4x for emission hot path                   │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│ Additional benefits:                                            │\n";
    std::cout << "│   - Better cache locality (no vtable chase)                     │\n";
    std::cout << "│   - Smaller object size (no vtable ptr per slot)                │\n";
    std::cout << "│   - Potential for inlining with LTO                             │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────┘\n\n";

    // ========================================================================
    // Section 6: Design recommendations
    // ========================================================================
    std::cout << "┌─────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ 6. Design Recommendations                                       │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│ ✓ Use inline function pointer for call (eliminates vtable)      │\n";
    std::cout << "│ ✓ Use uint8_t tag for slot type (only 7 types needed)           │\n";
    std::cout << "│ ✓ Pack hot data together (call_, connected_, blocked_, tag_)    │\n";
    std::cout << "│ ✓ Storage size: 48-64 bytes covers most callables               │\n";
    std::cout << "│ ✓ Support dynamic fallback for oversized callables              │\n";
    std::cout << "│ ✓ Use switch for cold paths (connected check for tracked)       │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "│ Storage recommendation: 64 bytes                                │\n";
    std::cout << "│   - Covers std::function (32 bytes on most platforms)           │\n";
    std::cout << "│   - Covers lambdas with moderate captures                       │\n";
    std::cout << "│   - Aligns well with cache lines                                │\n";
    std::cout << "│                                                                 │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────┘\n";

    return 0;
}
