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
// signal_inline_seqlock tests (lock-free seqlock)
// =============================================================================

TEST_CASE("signal_inline_seqlock basic emission", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<int> sig;
    int result = 0;
    
    sig.connect([&](int x) { result = x; });
    sig(42);
    
    REQUIRE(result == 42);
}

TEST_CASE("signal_inline_seqlock multiple slots", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<int> sig;
    int sum = 0;
    
    sig.connect([&](int x) { sum += x; });
    sig.connect([&](int x) { sum += x * 2; });
    
    sig(10);
    
    REQUIRE(sum == 30);  // 10 + 20
}

TEST_CASE("signal_inline_seqlock concurrent emission", "[signal_inline_seqlock][threaded]") {
    auto iteration = GENERATE_REPEAT();
    (void)iteration;
    
    sigslot::signal_inline_seqlock<int> sig;
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

TEST_CASE("signal_inline_seqlock member function", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<int> sig;
    Receiver r;
    
    sig.connect(&Receiver::on_signal, &r);
    sig(7);
    
    REQUIRE(r.value == 7);
}

TEST_CASE("signal_inline_seqlock block/unblock", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<int> sig;
    int result = 0;
    
    sig.connect([&](int x) { result = x; });
    
    sig.block();
    sig(42);
    REQUIRE(result == 0);
    
    sig.unblock();
    sig(42);
    REQUIRE(result == 42);
}

TEST_CASE("signal_inline_seqlock slot_count", "[signal_inline_seqlock]") {
    sigslot::signal_inline_seqlock<int> sig;
    
    REQUIRE(sig.slot_count() == 0);
    
    sig.connect([](int) {});
    REQUIRE(sig.slot_count() == 1);
    
    sig.connect([](int) {});
    REQUIRE(sig.slot_count() == 2);
    
    sig.disconnect_all();
    REQUIRE(sig.slot_count() == 0);
}

TEST_CASE("signal_inline_seqlock concurrent connect and emit", "[signal_inline_seqlock][threaded]") {
    auto iteration = GENERATE_REPEAT();
    (void)iteration;
    
    sigslot::signal_inline_seqlock<int> sig;
    std::atomic<int> sum{0};
    std::atomic<bool> done{false};
    
    // Start with one slot
    sig.connect([&](int x) { sum += x; });
    
    // Emitter thread
    std::thread emitter([&]() {
        while (!done) {
            sig(1);
        }
    });
    
    // Connect more slots while emitting
    for (int i = 0; i < 10; ++i) {
        sig.connect([&](int x) { sum += x; });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    done = true;
    emitter.join();
    
    // Just verify no crash - sum value depends on timing
    REQUIRE(sum > 0);
    REQUIRE(sig.slot_count() == 11);
}
