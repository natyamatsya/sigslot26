// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#include <catch2/catch_test_macros.hpp>
#include <sigslot/signal-inline.hpp>
#include "support/test-repeat.hpp"
#include <string>
#include <thread>
#include <vector>
#include <atomic>

// =============================================================================
// Test helpers
// =============================================================================

static std::atomic<int> g_sum{0};

void free_function(int i) {
    g_sum += i;
}

struct Receiver {
    int value = 0;
    void on_signal(int i) { value += i; }
    void on_signal_const(int i) const { g_sum += i; }
};

// =============================================================================
// signal_inline tests (single-threaded)
// =============================================================================

TEST_CASE("signal_inline basic emission", "[signal_inline]") {
    sigslot::signal_inline<int> sig;
    int result = 0;
    
    sig.connect([&](int x) { result = x; });
    sig(42);
    
    REQUIRE(result == 42);
}

TEST_CASE("signal_inline multiple slots", "[signal_inline]") {
    sigslot::signal_inline<int> sig;
    int sum = 0;
    
    sig.connect([&](int x) { sum += x; });
    sig.connect([&](int x) { sum += x * 2; });
    sig.connect([&](int x) { sum += x * 3; });
    
    sig(10);
    
    REQUIRE(sum == 60);  // 10 + 20 + 30
}

TEST_CASE("signal_inline free function", "[signal_inline]") {
    sigslot::signal_inline<int> sig;
    g_sum = 0;
    
    sig.connect(free_function);
    sig(5);
    
    REQUIRE(g_sum == 5);
}

TEST_CASE("signal_inline member function", "[signal_inline]") {
    sigslot::signal_inline<int> sig;
    Receiver r;
    
    sig.connect(&Receiver::on_signal, &r);
    sig(7);
    
    REQUIRE(r.value == 7);
}

TEST_CASE("signal_inline block/unblock", "[signal_inline]") {
    sigslot::signal_inline<int> sig;
    int result = 0;
    
    sig.connect([&](int x) { result = x; });
    
    sig.block();
    sig(42);
    REQUIRE(result == 0);
    
    sig.unblock();
    sig(42);
    REQUIRE(result == 42);
}

TEST_CASE("signal_inline disconnect_all", "[signal_inline]") {
    sigslot::signal_inline<int> sig;
    int result = 0;
    
    sig.connect([&](int x) { result = x; });
    REQUIRE(sig.slot_count() == 1);
    
    sig.disconnect_all();
    REQUIRE(sig.slot_count() == 0);
    
    sig(42);
    REQUIRE(result == 0);
}

TEST_CASE("signal_inline empty signal", "[signal_inline]") {
    sigslot::signal_inline<int> sig;
    
    REQUIRE(sig.empty());
    REQUIRE(sig.slot_count() == 0);
    
    sig.connect([](int) {});
    
    REQUIRE_FALSE(sig.empty());
    REQUIRE(sig.slot_count() == 1);
}

TEST_CASE("signal_inline multiple arguments", "[signal_inline]") {
    sigslot::signal_inline<int, std::string, double> sig;
    int i_result = 0;
    std::string s_result;
    double d_result = 0.0;
    
    sig.connect([&](int i, const std::string& s, double d) {
        i_result = i;
        s_result = s;
        d_result = d;
    });
    
    sig(42, "hello", 3.14);
    
    REQUIRE(i_result == 42);
    REQUIRE(s_result == "hello");
    REQUIRE(d_result == 3.14);
}

// =============================================================================
// signal_inline_rw tests (thread-safe with read-write lock)
// =============================================================================

TEST_CASE("signal_inline_rw basic emission", "[signal_inline_rw]") {
    sigslot::signal_inline_rw<int> sig;
    int result = 0;
    
    sig.connect([&](int x) { result = x; });
    sig(42);
    
    REQUIRE(result == 42);
}

TEST_CASE("signal_inline_rw concurrent emission", "[signal_inline_rw][threaded]") {
    auto iteration = GENERATE_REPEAT();
    (void)iteration;
    
    sigslot::signal_inline_rw<int> sig;
    std::atomic<int> sum{0};
    
    sig.connect([&](int x) { sum += x; });
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 1000; ++j) {
                sig(1);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    REQUIRE(sum == 4000);
}

TEST_CASE("signal_inline_rw member function", "[signal_inline_rw]") {
    sigslot::signal_inline_rw<int> sig;
    Receiver r;
    
    sig.connect(&Receiver::on_signal, &r);
    sig(7);
    
    REQUIRE(r.value == 7);
}

TEST_CASE("signal_inline_rw block/unblock", "[signal_inline_rw]") {
    sigslot::signal_inline_rw<int> sig;
    int result = 0;
    
    sig.connect([&](int x) { result = x; });
    
    sig.block();
    sig(42);
    REQUIRE(result == 0);
    
    sig.unblock();
    sig(42);
    REQUIRE(result == 42);
}

// =============================================================================
// signal_inline_rcu tests (lock-free RCU)
// =============================================================================

TEST_CASE("signal_inline_rcu basic emission", "[signal_inline_rcu]") {
    sigslot::signal_inline_rcu<int> sig;
    int result = 0;
    
    sig.connect([&](int x) { result = x; });
    sig(42);
    
    REQUIRE(result == 42);
}

TEST_CASE("signal_inline_rcu concurrent emission", "[signal_inline_rcu][threaded]") {
    auto iteration = GENERATE_REPEAT();
    (void)iteration;
    
    sigslot::signal_inline_rcu<int> sig;
    std::atomic<int> sum{0};
    
    sig.connect([&](int x) { sum += x; });
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 1000; ++j) {
                sig(1);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    REQUIRE(sum == 4000);
}

TEST_CASE("signal_inline_rcu member function", "[signal_inline_rcu]") {
    sigslot::signal_inline_rcu<int> sig;
    Receiver r;
    
    sig.connect(&Receiver::on_signal, &r);
    sig(7);
    
    REQUIRE(r.value == 7);
}

// =============================================================================
// fixed_vector tests
// =============================================================================

namespace {
    // Track construction/destruction for leak detection
    struct TrackedObject {
        static inline int constructions = 0;
        static inline int destructions = 0;
        static void reset() { constructions = 0; destructions = 0; }
        
        int value;
        TrackedObject(int v = 0) : value(v) { ++constructions; }
        TrackedObject(const TrackedObject& o) : value(o.value) { ++constructions; }
        TrackedObject(TrackedObject&& o) noexcept : value(o.value) { ++constructions; }
        ~TrackedObject() { ++destructions; }
    };
}

TEST_CASE("fixed_vector default construction", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 4> vec;
    
    REQUIRE(vec.size() == 0);
    REQUIRE(vec.empty());
    REQUIRE_FALSE(vec.full());
    REQUIRE(vec.capacity() == 4);
}

TEST_CASE("fixed_vector single element", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 4> vec;
    
    REQUIRE(vec.emplace_back(42));
    
    REQUIRE(vec.size() == 1);
    REQUIRE_FALSE(vec.empty());
    REQUIRE_FALSE(vec.full());
    REQUIRE(vec[0] == 42);
}

TEST_CASE("fixed_vector fill to capacity", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 4> vec;
    
    REQUIRE(vec.emplace_back(1));
    REQUIRE(vec.emplace_back(2));
    REQUIRE(vec.emplace_back(3));
    REQUIRE(vec.emplace_back(4));
    
    REQUIRE(vec.size() == 4);
    REQUIRE(vec.full());
    REQUIRE(vec[0] == 1);
    REQUIRE(vec[1] == 2);
    REQUIRE(vec[2] == 3);
    REQUIRE(vec[3] == 4);
}

TEST_CASE("fixed_vector overflow returns false", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 2> vec;
    
    REQUIRE(vec.emplace_back(1));
    REQUIRE(vec.emplace_back(2));
    REQUIRE_FALSE(vec.emplace_back(3));  // Should fail
    
    REQUIRE(vec.size() == 2);
    REQUIRE(vec.full());
    // Verify existing elements unchanged
    REQUIRE(vec[0] == 1);
    REQUIRE(vec[1] == 2);
}

TEST_CASE("fixed_vector clear", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 4> vec;
    
    vec.emplace_back(1);
    vec.emplace_back(2);
    vec.emplace_back(3);
    REQUIRE(vec.size() == 3);
    
    vec.clear();
    
    REQUIRE(vec.size() == 0);
    REQUIRE(vec.empty());
    REQUIRE_FALSE(vec.full());
}

TEST_CASE("fixed_vector clear calls destructors", "[fixed_vector]") {
    TrackedObject::reset();
    
    {
        sigslot::detail::fixed_vector<TrackedObject, 4> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);
        vec.emplace_back(3);
        
        REQUIRE(TrackedObject::constructions == 3);
        REQUIRE(TrackedObject::destructions == 0);
        
        vec.clear();
        
        REQUIRE(TrackedObject::destructions == 3);
    }
    
    // Destructor shouldn't double-destruct after clear
    REQUIRE(TrackedObject::destructions == 3);
}

TEST_CASE("fixed_vector destructor calls element destructors", "[fixed_vector]") {
    TrackedObject::reset();
    
    {
        sigslot::detail::fixed_vector<TrackedObject, 4> vec;
        vec.emplace_back(1);
        vec.emplace_back(2);
        
        REQUIRE(TrackedObject::constructions == 2);
        REQUIRE(TrackedObject::destructions == 0);
    }
    
    REQUIRE(TrackedObject::destructions == 2);
}

TEST_CASE("fixed_vector reuse after clear", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 2> vec;
    
    // Fill
    REQUIRE(vec.emplace_back(1));
    REQUIRE(vec.emplace_back(2));
    REQUIRE(vec.full());
    
    // Clear and refill
    vec.clear();
    REQUIRE(vec.empty());
    
    REQUIRE(vec.emplace_back(10));
    REQUIRE(vec.emplace_back(20));
    REQUIRE(vec.full());
    
    REQUIRE(vec[0] == 10);
    REQUIRE(vec[1] == 20);
}

TEST_CASE("fixed_vector with capacity 1", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 1> vec;
    
    REQUIRE(vec.capacity() == 1);
    REQUIRE(vec.empty());
    REQUIRE_FALSE(vec.full());
    
    REQUIRE(vec.emplace_back(42));
    REQUIRE(vec.full());
    REQUIRE_FALSE(vec.emplace_back(99));
    
    REQUIRE(vec[0] == 42);
}

TEST_CASE("fixed_vector data pointer stability", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 4> vec;
    
    vec.emplace_back(1);
    int* ptr1 = vec.data();
    
    vec.emplace_back(2);
    vec.emplace_back(3);
    int* ptr2 = vec.data();
    
    // Data pointer must remain stable (no reallocation)
    REQUIRE(ptr1 == ptr2);
}

TEST_CASE("fixed_vector const access", "[fixed_vector]") {
    sigslot::detail::fixed_vector<int, 4> vec;
    vec.emplace_back(42);
    
    const auto& const_vec = vec;
    
    REQUIRE(const_vec.size() == 1);
    REQUIRE(const_vec[0] == 42);
    REQUIRE(const_vec.data() != nullptr);
    REQUIRE_FALSE(const_vec.empty());
    REQUIRE_FALSE(const_vec.full());
    REQUIRE(const_vec.capacity() == 4);
}

TEST_CASE("fixed_vector with complex type", "[fixed_vector]") {
    sigslot::detail::fixed_vector<std::string, 3> vec;
    
    REQUIRE(vec.emplace_back("hello"));
    REQUIRE(vec.emplace_back("world"));
    REQUIRE(vec.emplace_back("!"));
    
    REQUIRE(vec[0] == "hello");
    REQUIRE(vec[1] == "world");
    REQUIRE(vec[2] == "!");
    
    vec.clear();
    REQUIRE(vec.empty());
}

TEST_CASE("fixed_vector emplace_back with multiple args", "[fixed_vector]") {
    struct Point {
        int x, y;
        Point(int x_, int y_) : x(x_), y(y_) {}
    };
    
    sigslot::detail::fixed_vector<Point, 2> vec;
    
    REQUIRE(vec.emplace_back(10, 20));
    REQUIRE(vec.emplace_back(30, 40));
    
    REQUIRE(vec[0].x == 10);
    REQUIRE(vec[0].y == 20);
    REQUIRE(vec[1].x == 30);
    REQUIRE(vec[1].y == 40);
}

// =============================================================================
// signal_inline_seqlock tests (lock-free with fixed capacity)
// =============================================================================

TEST_CASE("signal_inline_seqlock basic emission", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<16, int> sig;
    int result = 0;
    
    REQUIRE(sig.connect([&](int x) { result = x; }).has_value());
    sig(42);
    
    REQUIRE(result == 42);
}

TEST_CASE("signal_inline_seqlock multiple slots", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<16, int> sig;
    int sum = 0;
    
    REQUIRE(sig.connect([&](int x) { sum += x; }).has_value());
    REQUIRE(sig.connect([&](int x) { sum += x * 2; }).has_value());
    
    sig(10);
    
    REQUIRE(sum == 30);  // 10 + 20
}

TEST_CASE("signal_inline_seqlock capacity limit", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<4, int> sig;
    
    REQUIRE(sig.connect([](int) {}).has_value());
    REQUIRE(sig.connect([](int) {}).has_value());
    REQUIRE(sig.connect([](int) {}).has_value());
    REQUIRE(sig.connect([](int) {}).has_value());
    
    auto result = sig.connect([](int) {});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == sigslot::seqlock_error::capacity_exceeded);
    
    REQUIRE(sig.slot_count() == 4);
    REQUIRE(sig.full());
    REQUIRE(sig.max_slots() == 4);
}

TEST_CASE("signal_inline_seqlock concurrent emission", "[signal_inline_seqlock][threaded]") {
    auto iteration = GENERATE_REPEAT();
    (void)iteration;
    
    sigslot::signal_inline_seqlock<16, int> sig;
    std::atomic<int> sum{0};
    
    (void)sig.connect([&](int x) { sum += x; });
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 1000; ++j) {
                sig(1);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    REQUIRE(sum == 4000);
}

TEST_CASE("signal_inline_seqlock member function", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<16, int> sig;
    Receiver r;
    
    REQUIRE(sig.connect(&Receiver::on_signal, &r).has_value());
    sig(7);
    
    REQUIRE(r.value == 7);
}

TEST_CASE("signal_inline_seqlock block/unblock", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<16, int> sig;
    int result = 0;
    
    (void)sig.connect([&](int x) { result = x; });
    
    sig.block();
    sig(42);
    REQUIRE(result == 0);
    
    sig.unblock();
    sig(42);
    REQUIRE(result == 42);
}

TEST_CASE("signal_inline_seqlock slot_count", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<16, int> sig;
    
    REQUIRE(sig.slot_count() == 0);
    
    (void)sig.connect([](int) {});
    REQUIRE(sig.slot_count() == 1);
    
    (void)sig.connect([](int) {});
    REQUIRE(sig.slot_count() == 2);
    
    sig.disconnect_all();
    REQUIRE(sig.slot_count() == 0);
}

TEST_CASE("signal_inline_seqlock concurrent connect and emit", "[signal_inline_seqlock][threaded]") {
    auto iteration = GENERATE_REPEAT();
    (void)iteration;
    
    sigslot::signal_inline_seqlock<16, int> sig;
    std::atomic<int> sum{0};
    std::atomic<bool> done{false};
    
    // Start with one slot
    (void)sig.connect([&](int x) { sum += x; });
    
    // Emitter thread
    std::thread emitter([&]() {
        while (!done) {
            sig(1);
        }
    });
    
    // Connect more slots while emitting (up to capacity)
    for (int i = 0; i < 10; ++i) {
        (void)sig.connect([&](int x) { sum += x; });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    done = true;
    emitter.join();
    
    // Just verify no crash - sum value depends on timing
    REQUIRE(sum > 0);
    REQUIRE(sig.slot_count() == 11);
}

TEST_CASE("signal_inline_seqlock16 alias", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock16<int> sig;
    int result = 0;
    
    (void)sig.connect([&](int x) { result = x; });
    sig(42);
    
    REQUIRE(result == 42);
    REQUIRE(sig.max_slots() == 16);
}

TEST_CASE("signal_inline_seqlock expected error", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<2, int> sig;
    
    // First two should succeed
    REQUIRE(sig.connect([](int) {}).has_value());
    REQUIRE(sig.connect([](int) {}).has_value());
    
    // Third should return error
    auto result = sig.connect([](int) {});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == sigslot::seqlock_error::capacity_exceeded);
    REQUIRE(sig.slot_count() == 2);
}
