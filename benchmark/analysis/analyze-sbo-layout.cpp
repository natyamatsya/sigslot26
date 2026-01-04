#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <atomic>

// Include actual signal types to get real sizes
#include <sigslot/signal.hpp>
#include <sigslot/intrusive_ptr.hpp>

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
    analysis["type_sizes"]["std_array_int_3"] = sizeof(std::array<int,3>);
    analysis["type_sizes"]["std_function_void_int"] = sizeof(std::function<void(int)>);
    analysis["type_sizes"]["std_shared_ptr_int"] = sizeof(std::shared_ptr<int>);
    analysis["type_sizes"]["std_weak_ptr_int"] = sizeof(std::weak_ptr<int>);
    analysis["type_sizes"]["std_atomic_size_t"] = sizeof(std::atomic<size_t>);
    analysis["type_sizes"]["free_function_slot"] = sizeof(FreeFunctionSlot);
    analysis["type_sizes"]["member_function_slot"] = sizeof(MemberFunctionSlot);
    analysis["type_sizes"]["lambda_slot"] = sizeof(LambdaSlot);
    analysis["type_sizes"]["function_slot"] = sizeof(FunctionSlot);
    
    // Sigslot pointer types
    analysis["sigslot_types"]["slot_strong_ptr"] = sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>);
    analysis["sigslot_types"]["slot_weak_ptr"] = sizeof(sigslot::slot_weak_ptr<sigslot::detail::slot_state>);
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
    analysis["sigslot_types"]["mode"] = "dual-counter intrusive_ptr";
    analysis["sigslot_types"]["intrusive_refcount"] = sizeof(sigslot::detail::intrusive_refcount);
    analysis["sigslot_types"]["intrusive_ptr"] = sizeof(sigslot::detail::intrusive_ptr<sigslot::detail::intrusive_refcount>);
    analysis["sigslot_types"]["intrusive_weak_ptr"] = sizeof(sigslot::detail::intrusive_weak_ptr<sigslot::detail::intrusive_refcount>);
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
        freeFuncSizes.push_back({
            {"slots", i},
            {"total_bytes", i * sizeof(FreeFunctionSlot) + 16}
        });
    }
    analysis["free_functions"] = freeFuncSizes;
    
    json memberFuncSizes = json::array();
    for (int i = 1; i <= 3; ++i) {
        memberFuncSizes.push_back({
            {"slots", i},
            {"total_bytes", i * sizeof(MemberFunctionSlot) + 16}
        });
    }
    analysis["member_functions"] = memberFuncSizes;
    
    json functionSizes = json::array();
    for (int i = 1; i <= 2; ++i) {
        functionSizes.push_back({
            {"slots", i},
            {"total_bytes", i * sizeof(FunctionSlot) + 16}
        });
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
        "Fits well within cache line boundaries",
        "Covers common use cases (1-3 slots)",
        "Avoids heap allocation for most cases",
        "Good balance between stack usage and coverage"
    };
    
    // Write to JSON file
    std::ofstream file("sbo_analysis.json");
    file << analysis.dump(4) << std::endl;
    
    std::cout << "SBO analysis written to sbo_analysis.json\n\n";
#endif

    auto print_text_output = []() {
        std::cout << "=== SBO Binary Layout Analysis ===\n\n";
        
        std::cout << "Standard Library Type Sizes:\n";
        std::cout << "  std::vector<int>: " << sizeof(std::vector<int>) << " bytes\n";
        std::cout << "  std::array<int,3>: " << sizeof(std::array<int,3>) << " bytes\n";
        std::cout << "  std::function<void(int)>: " << sizeof(std::function<void(int)>) << " bytes\n";
        std::cout << "  std::shared_ptr<int>: " << sizeof(std::shared_ptr<int>) << " bytes\n";
        std::cout << "  std::weak_ptr<int>: " << sizeof(std::weak_ptr<int>) << " bytes\n";
        std::cout << "  std::atomic<size_t>: " << sizeof(std::atomic<size_t>) << " bytes\n";
        
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
        std::cout << "\n=== Dual-Counter Intrusive Ptr Mode (SIGSLOT_USE_INTRUSIVE_PTR=ON) ===\n\n";
        
        std::cout << "Intrusive Reference Counting Sizes:\n";
        std::cout << "  intrusive_refcount: " << sizeof(sigslot::detail::intrusive_refcount) << " bytes\n";
        std::cout << "    - m_strong (atomic<size_t>): " << sizeof(std::atomic<size_t>) << " bytes\n";
        std::cout << "    - m_weak (atomic<size_t>): " << sizeof(std::atomic<size_t>) << " bytes\n";
        std::cout << "    - m_arena_allocated (bool): " << sizeof(bool) << " byte\n";
        std::cout << "  intrusive_ptr<T>: " << sizeof(sigslot::detail::intrusive_ptr<sigslot::detail::intrusive_refcount>) << " bytes\n";
        std::cout << "  intrusive_weak_ptr<T>: " << sizeof(sigslot::detail::intrusive_weak_ptr<sigslot::detail::intrusive_refcount>) << " bytes\n";
        
        std::cout << "\nDual-Counter Benefits:\n";
        std::cout << "  - No std::weak_ptr anchor needed (saves 16 bytes per slot)\n";
        std::cout << "  - Lock-free weak_ptr::lock() via CAS\n";
        std::cout << "  - Single allocation (no control block)\n";
#else
        std::cout << "\n=== Standard shared_ptr Mode (SIGSLOT_USE_INTRUSIVE_PTR=OFF) ===\n\n";
        
        std::cout << "shared_ptr Overhead:\n";
        std::cout << "  std::shared_ptr<T>: " << sizeof(std::shared_ptr<int>) << " bytes\n";
        std::cout << "  std::weak_ptr<T>: " << sizeof(std::weak_ptr<int>) << " bytes\n";
        std::cout << "  Control block: ~32 bytes (separate allocation)\n";
#endif
        
        std::cout << "\nSlot Pointer Sizes:\n";
        std::cout << "  slot_strong_ptr: " << sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>) << " bytes\n";
        std::cout << "  slot_weak_ptr: " << sizeof(sigslot::slot_weak_ptr<sigslot::detail::slot_state>) << " bytes\n";
        
        std::cout << "\nSimulated Slot Type Sizes:\n";
        std::cout << "  FreeFunctionSlot: " << sizeof(FreeFunctionSlot) << " bytes\n";
        std::cout << "  MemberFunctionSlot: " << sizeof(MemberFunctionSlot) << " bytes\n";
        std::cout << "  LambdaSlot: " << sizeof(LambdaSlot) << " bytes\n";
        std::cout << "  FunctionSlot: " << sizeof(FunctionSlot) << " bytes\n";
        
        std::cout << "\nSBO Container Components:\n";
        std::cout << "  size_and_flag_: " << sizeof(size_t) << " bytes (high bit = heap flag)\n";
        std::cout << "  Total overhead: 16 bytes (rounded for alignment)\n";
        
        std::cout << "\nCache Line Analysis (64 bytes):\n";
        std::cout << "  3 slot_strong_ptr: " << 3 * sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>) << " bytes\n";
        std::cout << "  + SBO overhead: 16 bytes\n";
        std::cout << "  = Total for 3-slot SBO: " << 3 * sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>) + 16 << " bytes\n";
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
        std::cout << "  Fits in cache line: " << (3 * sizeof(sigslot::slot_strong_ptr<sigslot::detail::slot_state>) + 16 <= 64 ? "YES" : "NO") << "\n";
#endif
        
        std::cout << "\nMemory Efficiency:\n";
        std::cout << "  Heap allocation overhead: 24-32 bytes\n";
        std::cout << "  SBO saves heap allocation for first N slots\n";
        
        std::cout << "\nRecommendation:\n";
        std::cout << "  Optimal SBO size: 3 slots\n";
        std::cout << "  Reasons:\n";
        std::cout << "    - Fits well within cache line boundaries\n";
        std::cout << "    - Covers common use cases (1-3 slots)\n";
        std::cout << "    - Avoids heap allocation for most cases\n";
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
        std::cout << "    - Dual-counter intrusive_ptr fits perfectly\n";
#endif
    };

    print_text_output();
    
    return 0;
}
