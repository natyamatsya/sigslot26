#include <iostream>
#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <chrono>

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
    std::cout << "=== SBO Binary Layout Analysis ===\n\n";
    
    std::cout << "Type Sizes:\n";
    std::cout << "  sizeof(std::vector<int>): " << sizeof(std::vector<int>) << " bytes\n";
    std::cout << "  sizeof(std::array<int,3>): " << sizeof(std::array<int,3>) << " bytes\n";
    std::cout << "  sizeof(std::function<void(int)>): " << sizeof(std::function<void(int)>) << " bytes\n";
    std::cout << "  sizeof(std::shared_ptr<int>): " << sizeof(std::shared_ptr<int>) << " bytes\n";
    std::cout << "  sizeof(FreeFunctionSlot): " << sizeof(FreeFunctionSlot) << " bytes\n";
    std::cout << "  sizeof(MemberFunctionSlot): " << sizeof(MemberFunctionSlot) << " bytes\n";
    std::cout << "  sizeof(LambdaSlot): " << sizeof(LambdaSlot) << " bytes\n";
    std::cout << "  sizeof(FunctionSlot): " << sizeof(FunctionSlot) << " bytes\n";
    
    std::cout << "\nSBO Container Components:\n";
    std::cout << "  Union Storage: max(sizeof(vector), sizeof(array))\n";
    std::cout << "  bool using_heap_: " << sizeof(bool) << " byte\n";
    std::cout << "  size_t size_: " << sizeof(size_t) << " bytes\n";
    std::cout << "  Total overhead: ~9 bytes (rounded to 16 for alignment)\n";
    
    std::cout << "\nOptimal SBO Sizes Analysis:\n";
    
    // For free functions
    std::cout << "Free Functions (" << sizeof(FreeFunctionSlot) << " bytes each):\n";
    for (int i = 1; i <= 4; ++i) {
        size_t total = i * sizeof(FreeFunctionSlot) + 16; // 16 for overhead
        std::cout << "  " << i << " slots: " << total << " bytes\n";
    }
    
    // For member functions
    std::cout << "Member Functions (" << sizeof(MemberFunctionSlot) << " bytes each):\n";
    for (int i = 1; i <= 3; ++i) {
        size_t total = i * sizeof(MemberFunctionSlot) + 16;
        std::cout << "  " << i << " slots: " << total << " bytes\n";
    }
    
    // For std::function
    std::cout << "std::function (" << sizeof(FunctionSlot) << " bytes each):\n";
    for (int i = 1; i <= 2; ++i) {
        size_t total = i * sizeof(FunctionSlot) + 16;
        std::cout << "  " << i << " slots: " << total << " bytes\n";
    }
    
    std::cout << "\nCache Line Analysis:\n";
    std::cout << "  Typical cache line: 64 bytes\n";
    std::cout << "  SBO container with 3 free function slots: " 
              << (3 * sizeof(FreeFunctionSlot) + 16) << " bytes\n";
    std::cout << "  SBO container with 2 member function slots: " 
              << (2 * sizeof(MemberFunctionSlot) + 16) << " bytes\n";
    std::cout << "  SBO container with 1 std::function slot: " 
              << (1 * sizeof(FunctionSlot) + 16) << " bytes\n";
    
    std::cout << "\nMemory Efficiency:\n";
    std::cout << "  Heap allocation overhead: ~24-32 bytes\n";
    std::cout << "  SBO saves heap allocation for first N slots\n";
    std::cout << "  Break-even when saved allocation > SBO overhead\n";
    
    std::cout << "\nRecommendation:\n";
    std::cout << "  Size 3 is optimal because:\n";
    std::cout << "  - Fits well within cache line boundaries\n";
    std::cout << "  - Covers common use cases (1-3 slots)\n";
    std::cout << "  - Avoids heap allocation for most cases\n";
    std::cout << "  - Good balance between stack usage and coverage\n";
    
    return 0;
}
