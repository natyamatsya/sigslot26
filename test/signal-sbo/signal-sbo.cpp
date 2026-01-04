// Test SBO container implementation
#include <catch2/catch_test_macros.hpp>
#include <sigslot/signal-sbo.hpp>
#include <string>
#include <memory>

using namespace sigslot::detail;

TEST_CASE("SBO Container Basic Operations", "[sbo]") {
    SECTION("Empty container") {
        sbo_container<int> sbo;
        REQUIRE(sbo.empty());
        REQUIRE(sbo.size() == 0);
        REQUIRE(!sbo.is_using_heap());
    }
    
    SECTION("Stack storage") {
        sbo_container<int, 3> sbo;
        
        // Add up to capacity
        sbo.emplace_back(1);
        sbo.emplace_back(2);
        sbo.emplace_back(3);
        
        REQUIRE(sbo.size() == 3);
        REQUIRE(!sbo.is_using_heap());
        REQUIRE(sbo[0] == 1);
        REQUIRE(sbo[1] == 2);
        REQUIRE(sbo[2] == 3);
    }
    
    SECTION("Heap transition") {
        sbo_container<int, 2> sbo;
        
        // Fill stack
        sbo.emplace_back(1);
        sbo.emplace_back(2);
        REQUIRE(!sbo.is_using_heap());
        
        // Force heap transition
        sbo.emplace_back(3);
        REQUIRE(sbo.is_using_heap());
        REQUIRE(sbo.size() == 3);
        REQUIRE(sbo[0] == 1);
        REQUIRE(sbo[1] == 2);
        REQUIRE(sbo[2] == 3);
    }
    
    SECTION("Move operations") {
        sbo_container<std::string, 2> sbo1;
        sbo1.emplace_back("hello");
        sbo1.emplace_back("world");
        
        sbo_container<std::string, 2> sbo2 = std::move(sbo1);
        REQUIRE(sbo2.size() == 2);
        REQUIRE(sbo2[0] == "hello");
        REQUIRE(sbo2[1] == "world");
        REQUIRE(sbo1.empty());
    }
    
    SECTION("Pop back") {
        sbo_container<int, 3> sbo;
        sbo.emplace_back(1);
        sbo.emplace_back(2);
        sbo.emplace_back(3);
        
        sbo.pop_back();
        REQUIRE(sbo.size() == 2);
        REQUIRE(sbo.back() == 2);
        
        sbo.pop_back();
        REQUIRE(sbo.size() == 1);
        REQUIRE(sbo.back() == 1);
    }
    
    SECTION("Clear") {
        sbo_container<int, 2> sbo;
        sbo.emplace_back(1);
        sbo.emplace_back(2);
        sbo.emplace_back(3);  // Force heap
        
        REQUIRE(sbo.is_using_heap());
        sbo.clear();
        REQUIRE(sbo.empty());
        REQUIRE(!sbo.is_using_heap());
    }
}

TEST_CASE("SBO with Complex Types", "[sbo]") {
    struct TestStruct {
        int value;
        std::string name;
        
        TestStruct(int v, std::string n) : value(v), name(std::move(n)) {}
        
        TestStruct(const TestStruct&) = delete;
        TestStruct(TestStruct&& other) noexcept 
            : value(other.value), name(std::move(other.name)) {}
    };
    
    SECTION("Non-trivial types") {
        sbo_container<TestStruct, 2> sbo;
        
        sbo.emplace_back(1, "first");
        sbo.emplace_back(2, "second");
        
        REQUIRE(!sbo.is_using_heap());
        REQUIRE(sbo[0].value == 1);
        REQUIRE(sbo[0].name == "first");
        
        // Force heap
        sbo.emplace_back(3, "third");
        REQUIRE(sbo.is_using_heap());
        REQUIRE(sbo[2].value == 3);
        REQUIRE(sbo[2].name == "third");
    }
}

TEST_CASE("SBO Error Handling", "[sbo]") {
    SECTION("try_emplace_back success") {
        sbo_container<int, 2> sbo;
        
        auto result1 = sbo.try_emplace_back(1);
        REQUIRE(result1.has_value());
        REQUIRE(*result1 == 0);
        
        auto result2 = sbo.try_emplace_back(2);
        REQUIRE(result2.has_value());
        REQUIRE(*result2 == 1);
    }
    
    SECTION("try_reserve") {
        sbo_container<int, 2> sbo;
        
        // Stack capacity
        auto result1 = sbo.try_reserve(2);
        REQUIRE(result1.has_value());
        REQUIRE(!sbo.is_using_heap());
        
        // Force heap
        auto result2 = sbo.try_reserve(5);
        REQUIRE(result2.has_value());
        REQUIRE(sbo.is_using_heap());
    }
}

TEST_CASE("SBO Span Access", "[sbo]") {
    sbo_container<int, 3> sbo;
    sbo.emplace_back(10);
    sbo.emplace_back(20);
    sbo.emplace_back(30);
    
    SECTION("Non-const span") {
        auto span = sbo.get_span();
        REQUIRE(span.size() == 3);
        REQUIRE(span[0] == 10);
        REQUIRE(span[1] == 20);
        REQUIRE(span[2] == 30);
        
        // Modify through span
        span[1] = 25;
        REQUIRE(sbo[1] == 25);
    }
    
    SECTION("Const span") {
        const auto& const_sbo = sbo;
        auto span = const_sbo.get_span();
        REQUIRE(span.size() == 3);
        REQUIRE(span[0] == 10);
        REQUIRE(span[1] == 20);
        REQUIRE(span[2] == 30);
    }
}
