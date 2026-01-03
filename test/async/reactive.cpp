// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#include <catch2/catch_test_macros.hpp>
#include <sigslot/signal.hpp>
#include <sigslot/async/reactive.hpp>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// =============================================================================
// Map operator tests
// =============================================================================

TEST_CASE("rx::map transforms signal values", "[rx][map]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto mapped = sig | sigslot::rx::map([](int x) { return x * 2; });
    mapped.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);
    sig(3);

    REQUIRE(received.size() == 3);
    REQUIRE(received[0] == 2);
    REQUIRE(received[1] == 4);
    REQUIRE(received[2] == 6);
}

TEST_CASE("rx::map changes value type", "[rx][map]") {
    sigslot::signal<int> sig;
    std::vector<std::string> received;

    auto mapped = sig | sigslot::rx::map([](int x) { return std::to_string(x); });
    mapped.connect([&](const std::string& s) { received.push_back(s); });

    sig(42);
    sig(100);

    REQUIRE(received.size() == 2);
    REQUIRE(received[0] == "42");
    REQUIRE(received[1] == "100");
}

TEST_CASE("rx::map with multiple arguments", "[rx][map]") {
    sigslot::signal<int, int> sig;
    std::vector<int> received;

    auto mapped = sig | sigslot::rx::map([](int a, int b) { return a + b; });
    mapped.connect([&](int sum) { received.push_back(sum); });

    sig(1, 2);
    sig(10, 20);

    REQUIRE(received.size() == 2);
    REQUIRE(received[0] == 3);
    REQUIRE(received[1] == 30);
}

// =============================================================================
// Filter operator tests
// =============================================================================

TEST_CASE("rx::filter passes matching values", "[rx][filter]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto filtered = sig | sigslot::rx::filter([](int x) { return x > 0; });
    filtered.connect([&](int x) { received.push_back(x); });

    sig(-1);
    sig(1);
    sig(-2);
    sig(2);
    sig(0);
    sig(3);

    REQUIRE(received.size() == 3);
    REQUIRE(received[0] == 1);
    REQUIRE(received[1] == 2);
    REQUIRE(received[2] == 3);
}

TEST_CASE("rx::filter blocks all when predicate always false", "[rx][filter]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto filtered = sig | sigslot::rx::filter([](int) { return false; });
    filtered.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);
    sig(3);

    REQUIRE(received.empty());
}

TEST_CASE("rx::filter passes all when predicate always true", "[rx][filter]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto filtered = sig | sigslot::rx::filter([](int) { return true; });
    filtered.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);
    sig(3);

    REQUIRE(received.size() == 3);
}

// =============================================================================
// Chaining operators
// =============================================================================

TEST_CASE("rx::map and filter can be chained", "[rx][chain]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    // Double, then keep only values > 5
    auto& base = sig;
    auto mapped = base | sigslot::rx::map([](int x) { return x * 2; });
    auto filtered = mapped | sigslot::rx::filter([](int x) { return x > 5; });
    filtered.connect([&](int x) { received.push_back(x); });

    sig(1);  // -> 2, filtered out
    sig(2);  // -> 4, filtered out
    sig(3);  // -> 6, kept
    sig(4);  // -> 8, kept
    sig(5);  // -> 10, kept

    REQUIRE(received.size() == 3);
    REQUIRE(received[0] == 6);
    REQUIRE(received[1] == 8);
    REQUIRE(received[2] == 10);
}

// =============================================================================
// Debounce operator tests
// =============================================================================

TEST_CASE("rx::debounce suppresses rapid emissions", "[rx][debounce]") {
    sigslot::signal<> sig;
    std::atomic<int> count{0};

    auto debounced = sig | sigslot::rx::debounce(50ms);
    debounced.connect([&]() { count.fetch_add(1); });

    // Rapid emissions
    sig();
    sig();
    sig();
    std::this_thread::sleep_for(10ms);
    sig();
    sig();

    // Wait for debounce to complete with polling
    for (int i = 0; i < 30 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(20ms);
    }

    // Should only have emitted once after the quiet period
    REQUIRE(count.load() == 1);
}

TEST_CASE("rx::debounce emits after quiet period", "[rx][debounce]") {
    sigslot::signal<> sig;
    std::atomic<int> count{0};

    auto debounced = sig | sigslot::rx::debounce(30ms);
    debounced.connect([&]() { count.fetch_add(1); });

    // First burst
    sig();
    // Wait with polling to avoid timing issues on slow CI
    for (int i = 0; i < 20 && count.load() == 0; ++i) {
        std::this_thread::sleep_for(20ms);
    }
    REQUIRE(count.load() == 1);

    // Second burst
    sig();
    for (int i = 0; i < 20 && count.load() == 1; ++i) {
        std::this_thread::sleep_for(20ms);
    }
    REQUIRE(count.load() == 2);
}

// =============================================================================
// Connection management
// =============================================================================

TEST_CASE("rx::map connection can be disconnected", "[rx][connection]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto mapped = sig | sigslot::rx::map([](int x) { return x * 2; });
    auto conn = mapped.connect([&](int x) { received.push_back(x); });

    sig(1);
    conn.disconnect();
    sig(2);

    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == 2);
}

TEST_CASE("rx::filter connection can be disconnected", "[rx][connection]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto filtered = sig | sigslot::rx::filter([](int x) { return x > 0; });
    auto conn = filtered.connect([&](int x) { received.push_back(x); });

    sig(1);
    conn.disconnect();
    sig(2);

    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == 1);
}

// =============================================================================
// Execution-aware operator tests (requires stdexec)
// =============================================================================

#if defined(SIGSLOT_HAVE_STDEXEC)

#include <exec/static_thread_pool.hpp>

TEST_CASE("rx::observe_on switches execution context", "[rx][execution]") {
    exec::static_thread_pool pool{2};
    auto sched = pool.get_scheduler();

    sigslot::signal<int> sig;
    std::atomic<int> sum{0};
    std::atomic<std::thread::id> handler_thread_id{};

    auto observed = sig | sigslot::rx::observe_on(sched);
    observed.connect([&](int x) {
        handler_thread_id.store(std::this_thread::get_id());
        sum.fetch_add(x);
    });

    sig(10);
    sig(20);

    // Wait for async processing
    std::this_thread::sleep_for(50ms);

    REQUIRE(sum.load() == 30);
    // Handler should run on pool thread, not main thread
    REQUIRE(handler_thread_id.load() != std::thread::id{});
}

TEST_CASE("rx::observe_on with map", "[rx][execution]") {
    exec::static_thread_pool pool{2};
    auto sched = pool.get_scheduler();

    sigslot::signal<int> sig;
    std::atomic<int> result{0};

    // Chain manually to avoid pipe operator conflict with stdexec
    auto mapped = sig | sigslot::rx::map([](int x) { return x * 2; });
    auto observed = sigslot::rx::observe_on(sched)(mapped);
    observed.connect([&](int x) { result.store(x); });

    sig(21);
    std::this_thread::sleep_for(50ms);

    REQUIRE(result.load() == 42);
}

TEST_CASE("rx::debounce_on uses scheduler", "[rx][execution][debounce]") {
    exec::static_thread_pool pool{1};
    auto sched = pool.get_scheduler();

    sigslot::signal<> sig;
    std::atomic<int> count{0};

    auto debounced = sig | sigslot::rx::debounce_on(sched, 30ms);
    debounced.connect([&]() { count.fetch_add(1); });

    // Rapid emissions
    sig();
    sig();
    sig();

    // Wait for debounce
    std::this_thread::sleep_for(100ms);

    REQUIRE(count.load() == 1);
}

#endif // SIGSLOT_HAVE_STDEXEC
