#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <functional>
#include <memory>

// Try to include nlohmann/json, fallback to simple output if not available
#ifdef HAS_NLOHMANN_JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#define USE_JSON 1
#else
#define USE_JSON 0
#endif

// Simulate slot types
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
    
    // Type sizes
    analysis["type_sizes"]["std_vector_int"] = sizeof(std::vector<int>);
    analysis["type_sizes"]["std_array_int_3"] = sizeof(std::array<int,3>);
    analysis["type_sizes"]["std_function_void_int"] = sizeof(std::function<void(int)>);
    analysis["type_sizes"]["std_shared_ptr_int"] = sizeof(std::shared_ptr<int>);
    analysis["type_sizes"]["free_function_slot"] = sizeof(FreeFunctionSlot);
    analysis["type_sizes"]["member_function_slot"] = sizeof(MemberFunctionSlot);
    analysis["type_sizes"]["lambda_slot"] = sizeof(LambdaSlot);
    analysis["type_sizes"]["function_slot"] = sizeof(FunctionSlot);
    
    // SBO container components
    analysis["sbo_components"]["bool_using_heap"] = sizeof(bool);
    analysis["sbo_components"]["size_t_size"] = sizeof(size_t);
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
        
        std::cout << "Type Sizes:\n";
        std::cout << "  std::vector<int>: " << sizeof(std::vector<int>) << " bytes\n";
        std::cout << "  std::array<int,3>: " << sizeof(std::array<int,3>) << " bytes\n";
        std::cout << "  std::function<void(int)>: " << sizeof(std::function<void(int)>) << " bytes\n";
        std::cout << "  std::shared_ptr<int>: " << sizeof(std::shared_ptr<int>) << " bytes\n";
        std::cout << "  FreeFunctionSlot: " << sizeof(FreeFunctionSlot) << " bytes\n";
        std::cout << "  MemberFunctionSlot: " << sizeof(MemberFunctionSlot) << " bytes\n";
        std::cout << "  LambdaSlot: " << sizeof(LambdaSlot) << " bytes\n";
        std::cout << "  FunctionSlot: " << sizeof(FunctionSlot) << " bytes\n";
        
        std::cout << "\nSBO Container Components:\n";
        std::cout << "  bool using_heap_: " << sizeof(bool) << " byte\n";
        std::cout << "  size_t size_: " << sizeof(size_t) << " bytes\n";
        std::cout << "  Total overhead: 16 bytes (rounded for alignment)\n";
        
        std::cout << "\nOptimal SBO Sizes Analysis:\n";
        std::cout << "  Free Function Slot Size: " << sizeof(FreeFunctionSlot) << " bytes\n";
        std::cout << "  Member Function Slot Size: " << sizeof(MemberFunctionSlot) << " bytes\n";
        std::cout << "  Function Slot Size: " << sizeof(FunctionSlot) << " bytes\n";
        
        std::cout << "\nFree Functions (" << sizeof(FreeFunctionSlot) << " bytes each):\n";
        for (int i = 1; i <= 4; ++i) {
            std::cout << "  " << i << " slots: " << i * sizeof(FreeFunctionSlot) + 16 << " bytes\n";
        }
        
        std::cout << "\nMember Functions (" << sizeof(MemberFunctionSlot) << " bytes each):\n";
        for (int i = 1; i <= 3; ++i) {
            std::cout << "  " << i << " slots: " << i * sizeof(MemberFunctionSlot) + 16 << " bytes\n";
        }
        
        std::cout << "\nstd::function (" << sizeof(FunctionSlot) << " bytes each):\n";
        for (int i = 1; i <= 2; ++i) {
            std::cout << "  " << i << " slots: " << i * sizeof(FunctionSlot) + 16 << " bytes\n";
        }
        
        std::cout << "\nCache Line Analysis:\n";
        std::cout << "  Typical cache line: 64 bytes\n";
        std::cout << "  3 free function slots: " << 3 * sizeof(FreeFunctionSlot) + 16 << " bytes\n";
        std::cout << "  2 member function slots: " << 2 * sizeof(MemberFunctionSlot) + 16 << " bytes\n";
        std::cout << "  1 function slot: " << 1 * sizeof(FunctionSlot) + 16 << " bytes\n";
        
        std::cout << "\nMemory Efficiency:\n";
        std::cout << "  Heap allocation overhead: 24-32 bytes\n";
        std::cout << "  SBO saves heap allocation for first N slots\n";
        
        std::cout << "\nRecommendation:\n";
        std::cout << "  Optimal size: 3\n";
        std::cout << "  Reasons:\n";
        std::cout << "    - Fits well within cache line boundaries\n";
        std::cout << "    - Covers common use cases (1-3 slots)\n";
        std::cout << "    - Avoids heap allocation for most cases\n";
        std::cout << "    - Good balance between stack usage and coverage\n";
    };

    print_text_output();
    
    return 0;
}
