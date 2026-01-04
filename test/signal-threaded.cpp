// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: palacaze/sigslot contributors

#include <catch2/catch_test_macros.hpp>
#include <sigslot/signal.hpp>
#include <thread>
#include <atomic>
#include <array>
#include "support/test-repeat.hpp"

// Test configuration constants
namespace {
constexpr int EMISSIONS_PER_ITERATION = 10000;
constexpr int THREAD_COUNT = 10;
constexpr int ITERATIONS_PER_THREAD = 100;
constexpr int EMISSIONS_PER_THREAD_ITERATION = 100;
// Note: Threaded Crossed doesn't use GENERATE_REPEAT - it's already a stress test
constexpr int CROSS_EMISSION_COUNT = 1000000;
constexpr std::int64_t EXPECTED_CROSS_SUM = 1000000000000ll;
} // namespace


static std::atomic<std::int64_t> sum{0};

static void f(int i) {
    sum += i;
}
static void f1(int i) {
    sum += i;
}
static void f2(int i) {
    sum += i;
}
static void f3(int i) {
    sum += i;
}

static void emit_many(sigslot::signal<int>& sig) {
    for (int i = 0; i < EMISSIONS_PER_ITERATION; ++i)
        sig(1);
}

static void connect_emit(sigslot::signal<int>& sig) {
    for (int i = 0; i < ITERATIONS_PER_THREAD; ++i) {
        auto s = sig.connect_scoped(f);
        for (int j = 0; j < EMISSIONS_PER_THREAD_ITERATION; ++j)
            sig(1);
    }
}

static void connect_cross(sigslot::signal<int>& s1, sigslot::signal<int>& s2,
                          std::atomic<int>& go) {
    auto cross = s1.connect([&](int i) {
        if (i & 1)
            f(i);
        else
            s2(i + 1);
    });

    go++;
    while (go != 3)
        std::this_thread::yield();

    for (int i = 0; i < CROSS_EMISSION_COUNT; ++i)
        s1(i);
}

TEST_CASE("Threaded Mix", "[signal_threaded]") {
    auto iteration = GENERATE(GENERATE_REPEAT());
    (void)iteration;

    sum = 0;

    sigslot::signal<int> sig;

    std::array<std::thread, 10> threads;
    for (auto& t : threads)
        t = std::thread(connect_emit, std::ref(sig));

    for (auto& t : threads)
        t.join();
}

TEST_CASE("Threaded Emission", "[signal_threaded]") {
    auto iteration = GENERATE(GENERATE_REPEAT());
    (void)iteration;

    sum = 0;

    sigslot::signal<int> sig;
    sig.connect(f);

    std::array<std::thread, 10> threads;
    for (auto& t : threads)
        t = std::thread(emit_many, std::ref(sig));

    for (auto& t : threads)
        t.join();

    REQUIRE(sum == EMISSIONS_PER_ITERATION * THREAD_COUNT);
}

// test for deadlocks in cross emission situation
// Note: This is already a stress test with 1M emissions, no need for GENERATE_REPEAT
TEST_CASE("Threaded Crossed", "[signal_threaded]") {
    sum = 0;

    sigslot::signal<int> sig1;
    sigslot::signal<int> sig2;

    std::atomic<int> go{0};

    std::thread t1(connect_cross, std::ref(sig1), std::ref(sig2), std::ref(go));
    std::thread t2(connect_cross, std::ref(sig2), std::ref(sig1), std::ref(go));

    while (go != 2)
        std::this_thread::yield();
    go++;

    t1.join();
    t2.join();

    REQUIRE(sum == EXPECTED_CROSS_SUM);
}

// test what happens when more than one thread attempt disconnection
TEST_CASE("Threaded Misc", "[signal_threaded]") {
    auto iteration = GENERATE(GENERATE_REPEAT());
    (void)iteration;

    sum = 0;
    sigslot::signal<int> sig;
    
    // Use operation counts instead of time-based delays
    // Scale inversely with repeat count to keep total work constant
    constexpr int OPS_PER_THREAD = 1000 / SIGSLOT_TEST_REPEAT;
    std::atomic<int> emitter_done{0};
    std::atomic<int> conn_done{0};
    std::atomic<int> disconn_done{0};

    auto emitter = [&] {
        for (int op = 0; op < OPS_PER_THREAD; ++op) {
            sig(1);
        }
        emitter_done++;
    };

    auto conn = [&] {
        for (int op = 0; op < OPS_PER_THREAD / 30; ++op) {
            for (int i = 0; i < 10; ++i) {
                sig.connect(f1);
                sig.connect(f2);
                sig.connect(f3);
            }
        }
        conn_done++;
    };

    auto disconn = [&] {
        for (int op = 0; op < OPS_PER_THREAD; ++op) {
            if (op % 3 == 0)
                sig.disconnect(f1);
            else if (op % 3 == 1)
                sig.disconnect(f2);
            else
                sig.disconnect(f3);
        }
        disconn_done++;
    };

    std::array<std::thread, 20> emitters;
    std::array<std::thread, 20> conns;
    std::array<std::thread, 20> disconns;

    for (auto& t : conns)
        t = std::thread(conn);
    for (auto& t : emitters)
        t = std::thread(emitter);
    for (auto& t : disconns)
        t = std::thread(disconn);

    // Join all threads (proper handshake - no timing delays)
    for (auto& t : emitters)
        t.join();
    for (auto& t : disconns)
        t.join();
    for (auto& t : conns)
        t.join();
}
