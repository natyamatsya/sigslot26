#include <print>
#include <fstream>
#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <atomic>

// Include actual signal types to get real sizes
#include <sigslot/signal.hpp>
#include <sigslot/intrusive-ptr.hpp>

// Try to include nlohmann/json, fallback to simple output if not available
#ifdef HAS_NLOHMANN_JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#define USE_JSON 1
#else
#define USE_JSON 0
#endif

// Simulate slot types for comparison
struct FreeFunctionSlot {
    void (*func)(int);
};

struct MemberFunctionSlot {
    void (MemberFunctionSlot::*func)(int);
    MemberFunctionSlot* obj;
};

struct LambdaSlot {
    void* func;
    void* capture;
};

struct FunctionSlot {
    std::function<void(int)> func;
};

int main() {
#if USE_JSON
    json analysis;

    // Standard library type sizes
    analysis["type_sizes"]["std_vector_int"] = sizeof(std::vector<int>);
    analysis["type_sizes"]["std_array_int_3"] = sizeof(std::array<int, 3>);
    analysis["type_sizes"]["std_function_void_int"] = sizeof(std::function<void(int)>);
    analysis["type_sizes"]["std_shared_ptr_int"] = sizeof(std::shared_ptr<int>);
    analysis["type_sizes"]["std_weak_ptr_int"] = sizeof(std::weak_ptr<int>);
    analysis["type_sizes"]["std_atomic_size_t"] = sizeof(std::atomic<size_t>);
    analysis["type_sizes"]["free_function_slot"] = sizeof(FreeFunctionSlot);
    analysis["type_sizes"]["member_function_slot"] = sizeof(MemberFunctionSlot);
    analysis["type_sizes"]["lambda_slot"] = sizeof(LambdaSlot);
    analysis["type_sizes"]["function_slot"] = sizeof(FunctionSlot);

    // Sigslot pointer types
    analysis["sigslot_types"]["slot_strong_ptr"] =
        sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>);
    analysis["sigslot_types"]["slot_weak_ptr"] =
        sizeof(sigslot::slot_weak_ptr<sigslot::detail::slot_state>);
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
    analysis["sigslot_types"]["mode"] = "dual-counter intrusive_ptr";
    analysis["sigslot_types"]["intrusive_refcount"] = sizeof(sigslot::detail::intrusive_refcount);
    analysis["sigslot_types"]["intrusive_ptr"] =
        sizeof(sigslot::detail::intrusive_ptr<sigslot::detail::intrusive_refcount>);
    analysis["sigslot_types"]["intrusive_weak_ptr"] =
        sizeof(sigslot::detail::intrusive_weak_ptr<sigslot::detail::intrusive_refcount>);
#else
    analysis["sigslot_types"]["mode"] = "std::shared_ptr";
#endif

    // SBO container components
    analysis["sbo_components"]["size_and_flag"] = sizeof(size_t);
    analysis["sbo_components"]["overhead_bytes"] = 16; // rounded for alignment

    // Optimal SBO sizes analysis
    analysis["optimal_sizes"]["free_function_slot_size"] = sizeof(FreeFunctionSlot);
    analysis["optimal_sizes"]["member_function_slot_size"] = sizeof(MemberFunctionSlot);
    analysis["optimal_sizes"]["function_slot_size"] = sizeof(FunctionSlot);

    // Calculate sizes for different slot counts
    json freeFuncSizes = json::array();
    for (int i = 1; i <= 4; ++i) {
        freeFuncSizes.push_back({{"slots", i}, {"total_bytes", i * sizeof(FreeFunctionSlot) + 16}});
    }
    analysis["free_functions"] = freeFuncSizes;

    json memberFuncSizes = json::array();
    for (int i = 1; i <= 3; ++i) {
        memberFuncSizes.push_back(
            {{"slots", i}, {"total_bytes", i * sizeof(MemberFunctionSlot) + 16}});
    }
    analysis["member_functions"] = memberFuncSizes;

    json functionSizes = json::array();
    for (int i = 1; i <= 2; ++i) {
        functionSizes.push_back({{"slots", i}, {"total_bytes", i * sizeof(FunctionSlot) + 16}});
    }
    analysis["std_functions"] = functionSizes;

    // Cache line analysis
    analysis["cache_line"]["typical_size"] = 64;
    analysis["cache_line"]["three_free_slots_bytes"] = 3 * sizeof(FreeFunctionSlot) + 16;
    analysis["cache_line"]["two_member_slots_bytes"] = 2 * sizeof(MemberFunctionSlot) + 16;
    analysis["cache_line"]["one_function_slot_bytes"] = 1 * sizeof(FunctionSlot) + 16;

    // Memory efficiency
    analysis["memory_efficiency"]["heap_overhead_min"] = 24;
    analysis["memory_efficiency"]["heap_overhead_max"] = 32;

    // Recommendation
    analysis["recommendation"]["optimal_size"] = 3;
    analysis["recommendation"]["reasons"] = {
        "Fits well within cache line boundaries", "Covers common use cases (1-3 slots)",
        "Avoids heap allocation for most cases", "Good balance between stack usage and coverage"};

    // Write to JSON file
    std::ofstream file("sbo_analysis.json");
    file << analysis.dump(4) << std::endl;

    std::println("SBO analysis written to sbo_analysis.json\n");
#endif

    auto print_text_output = []() {
        std::println("=== SBO Binary Layout Analysis ===\n");

        std::println("Standard Library Type Sizes:");
        std::println("  std::vector<int>: {} bytes", sizeof(std::vector<int>));
        std::println("  std::array<int,3>: {} bytes", sizeof(std::array<int, 3>));
        std::println("  std::function<void(int)>: {} bytes", sizeof(std::function<void(int)>));
        std::println("  std::shared_ptr<int>: {} bytes", sizeof(std::shared_ptr<int>));
        std::println("  std::weak_ptr<int>: {} bytes", sizeof(std::weak_ptr<int>));
        std::println("  std::atomic<size_t>: {} bytes", sizeof(std::atomic<size_t>));

#ifdef SIGSLOT_USE_INTRUSIVE_PTR
        std::println("\n=== Dual-Counter Intrusive Ptr Mode (SIGSLOT_USE_INTRUSIVE_PTR=ON) ===\n");

        std::println("Intrusive Reference Counting Sizes:");
        std::println("  intrusive_refcount: {} bytes", sizeof(sigslot::detail::intrusive_refcount));
        std::println("    - m_strong (atomic<size_t>): {} bytes", sizeof(std::atomic<size_t>));
        std::println("    - m_weak (atomic<size_t>): {} bytes", sizeof(std::atomic<size_t>));
        std::println("    - m_arena_allocated (bool): {} byte", sizeof(bool));
        std::println("  intrusive_ptr<T>: {} bytes",
                     sizeof(sigslot::detail::intrusive_ptr<sigslot::detail::intrusive_refcount>));
        std::println(
            "  intrusive_weak_ptr<T>: {} bytes",
            sizeof(sigslot::detail::intrusive_weak_ptr<sigslot::detail::intrusive_refcount>));

        std::println("\nDual-Counter Benefits:");
        std::println("  - No std::weak_ptr anchor needed (saves 16 bytes per slot)");
        std::println("  - Lock-free weak_ptr::lock() via CAS");
        std::println("  - Single allocation (no control block)");
#else
        std::println("\n=== Standard shared_ptr Mode (SIGSLOT_USE_INTRUSIVE_PTR=OFF) ===\n");

        std::println("shared_ptr Overhead:");
        std::println("  std::shared_ptr<T>: {} bytes", sizeof(std::shared_ptr<int>));
        std::println("  std::weak_ptr<T>: {} bytes", sizeof(std::weak_ptr<int>));
        std::println("  Control block: ~32 bytes (separate allocation)");
#endif

        std::println("\nSlot Pointer Sizes:");
        std::println("  slot_strong_ptr: {} bytes",
                     sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>));
        std::println("  slot_weak_ptr: {} bytes",
                     sizeof(sigslot::slot_weak_ptr<sigslot::detail::slot_state>));

        std::println("\nSimulated Slot Type Sizes:");
        std::println("  FreeFunctionSlot: {} bytes", sizeof(FreeFunctionSlot));
        std::println("  MemberFunctionSlot: {} bytes", sizeof(MemberFunctionSlot));
        std::println("  LambdaSlot: {} bytes", sizeof(LambdaSlot));
        std::println("  FunctionSlot: {} bytes", sizeof(FunctionSlot));

        std::println("\nSBO Container Components:");
        std::println("  size_and_flag_: {} bytes (high bit = heap flag)", sizeof(size_t));
        std::println("  Total overhead: 16 bytes (rounded for alignment)");

        std::println("\nCache Line Analysis (64 bytes):");
        std::println("  3 slot_strong_ptr: {} bytes",
                     3 * sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>));
        std::println("  + SBO overhead: 16 bytes");
        std::println("  = Total for 3-slot SBO: {} bytes",
                     3 * sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>) + 16);
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
        std::println("  Fits in cache line: {}",
                     (3 * sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>) + 16 <= 64
                          ? "YES"
                          : "NO"));
#endif

        std::println("\nMemory Efficiency:");
        std::println("  Heap allocation overhead: 24-32 bytes");
        std::println("  SBO saves heap allocation for first N slots");

        std::println("\nRecommendation:");
        std::println("  Optimal SBO size: 3 slots");
        std::println("  Reasons:");
        std::println("    - Fits well within cache line boundaries");
        std::println("    - Covers common use cases (1-3 slots)");
        std::println("    - Avoids heap allocation for most cases");
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
        std::println("    - Dual-counter intrusive_ptr fits perfectly");
#endif
    };

    print_text_output();

    return 0;
}
