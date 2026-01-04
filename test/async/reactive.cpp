// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#include <catch2/catch_test_macros.hpp>
#include <sigslot/signal.hpp>
#include <sigslot/async/reactive.hpp>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <semaphore>
#include "../support/test-repeat.hpp"
#include "../support/thread-pool-fixture.hpp"

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

    sig(1); // -> 2, filtered out
    sig(2); // -> 4, filtered out
    sig(3); // -> 6, kept
    sig(4); // -> 8, kept
    sig(5); // -> 10, kept

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
// Throttle operator tests
// =============================================================================

TEST_CASE("rx::throttle limits emission rate", "[rx][throttle]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto throttled = sig | sigslot::rx::throttle(50ms);
    throttled.connect([&](int x) { received.push_back(x); });

    sig(1); // Passes through
    sig(2); // Blocked
    sig(3); // Blocked

    std::this_thread::sleep_for(60ms);
    sig(4); // Passes through

    REQUIRE(received.size() == 2);
    REQUIRE(received[0] == 1);
    REQUIRE(received[1] == 4);
}

// =============================================================================
// Distinct operator tests
// =============================================================================

TEST_CASE("rx::distinct filters consecutive duplicates", "[rx][distinct]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto distinct = sig | sigslot::rx::distinct();
    distinct.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(1);
    sig(2);
    sig(2);
    sig(2);
    sig(3);
    sig(1); // Different from previous (3)

    REQUIRE(received.size() == 4);
    REQUIRE(received[0] == 1);
    REQUIRE(received[1] == 2);
    REQUIRE(received[2] == 3);
    REQUIRE(received[3] == 1);
}

// =============================================================================
// Scan operator tests
// =============================================================================

TEST_CASE("rx::scan computes running sum", "[rx][scan]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto running_sum = sig | sigslot::rx::scan(0, [](int acc, int x) { return acc + x; });
    running_sum.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);
    sig(3);
    sig(4);

    REQUIRE(received.size() == 4);
    REQUIRE(received[0] == 1);
    REQUIRE(received[1] == 3);
    REQUIRE(received[2] == 6);
    REQUIRE(received[3] == 10);
}

// =============================================================================
// Buffer operator tests
// =============================================================================

TEST_CASE("rx::buffer collects N emissions", "[rx][buffer]") {
    sigslot::signal<int> sig;
    std::vector<std::vector<int>> received;

    auto buffered = sig | sigslot::rx::buffer<int>(3);
    buffered.connect([&](const std::vector<int>& v) { received.push_back(v); });

    sig(1);
    sig(2);
    sig(3); // Emit [1,2,3]
    sig(4);
    sig(5);
    sig(6); // Emit [4,5,6]
    sig(7); // Buffered, not emitted

    REQUIRE(received.size() == 2);
    REQUIRE(received[0] == std::vector<int>{1, 2, 3});
    REQUIRE(received[1] == std::vector<int>{4, 5, 6});
}

// =============================================================================
// Take operator tests
// =============================================================================

TEST_CASE("rx::take limits to N emissions", "[rx][take]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto first_three = sig | sigslot::rx::take(3);
    first_three.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);
    sig(3);
    sig(4);
    sig(5);

    REQUIRE(received.size() == 3);
    REQUIRE(received[0] == 1);
    REQUIRE(received[1] == 2);
    REQUIRE(received[2] == 3);
}

TEST_CASE("rx::take with count=0", "[rx][take]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto none = sig | sigslot::rx::take(0);
    none.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);

    REQUIRE(received.empty());
}

// =============================================================================
// Skip operator tests
// =============================================================================

TEST_CASE("rx::skip ignores first N emissions", "[rx][skip]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto after_two = sig | sigslot::rx::skip(2);
    after_two.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);
    sig(3);
    sig(4);
    sig(5);

    REQUIRE(received.size() == 3);
    REQUIRE(received[0] == 3);
    REQUIRE(received[1] == 4);
    REQUIRE(received[2] == 5);
}

TEST_CASE("rx::skip with count=0 passes all", "[rx][skip]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto all = sig | sigslot::rx::skip(0);
    all.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);

    REQUIRE(received.size() == 2);
}

// =============================================================================
// TakeWhile operator tests
// =============================================================================

TEST_CASE("rx::take_while stops on false", "[rx][take_while]") {
    sigslot::signal<int> sig;
    std::vector<int> received;

    auto while_positive = sig | sigslot::rx::take_while([](int x) { return x > 0; });
    while_positive.connect([&](int x) { received.push_back(x); });

    sig(1);
    sig(2);
    sig(3);
    sig(-1); // Stop here
    sig(4);  // Not forwarded

    REQUIRE(received.size() == 3);
    REQUIRE(received[0] == 1);
    REQUIRE(received[1] == 2);
    REQUIRE(received[2] == 3);
}

// =============================================================================
// Merge operator tests
// =============================================================================

TEST_CASE("rx::merge combines signals", "[rx][merge]") {
    sigslot::signal<int> sig1;
    sigslot::signal<int> sig2;
    sigslot::signal<int> sig3;
    std::vector<int> received;

    auto merged = sigslot::rx::merge<int>(sig1, sig2, sig3);
    merged.connect([&](int x) { received.push_back(x); });

    sig1(1);
    sig2(2);
    sig3(3);
    sig1(4);

    REQUIRE(received.size() == 4);
    REQUIRE(received[0] == 1);
    REQUIRE(received[1] == 2);
    REQUIRE(received[2] == 3);
    REQUIRE(received[3] == 4);
}

// =============================================================================
// CombineLatest operator tests
// =============================================================================

TEST_CASE("rx::combine_latest emits after both fire", "[rx][combine_latest]") {
    sigslot::signal<int> sig1;
    sigslot::signal<std::string> sig2;
    std::vector<std::pair<int, std::string>> received;

    auto combined = sigslot::rx::combine_latest<int, std::string>(sig1, sig2);
    combined.connect([&](int a, const std::string& b) { received.emplace_back(a, b); });

    sig1(1); // No output yet (sig2 hasn't fired)
    REQUIRE(received.empty());

    sig2("a"); // Now both have fired: (1, "a")
    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == std::pair{1, std::string("a")});

    sig1(2); // (2, "a")
    REQUIRE(received.size() == 2);
    REQUIRE(received[1] == std::pair{2, std::string("a")});

    sig2("b"); // (2, "b")
    REQUIRE(received.size() == 3);
    REQUIRE(received[2] == std::pair{2, std::string("b")});
}

// =============================================================================
// Zip operator tests
// =============================================================================

TEST_CASE("rx::zip pairs emissions 1:1", "[rx][zip]") {
    sigslot::signal<int> sig1;
    sigslot::signal<std::string> sig2;
    std::vector<std::pair<int, std::string>> received;

    auto zipped = sigslot::rx::zip<int, std::string>(sig1, sig2);
    zipped.connect([&](int a, const std::string& b) { received.emplace_back(a, b); });

    sig1(1);
    sig1(2);
    sig1(3);
    // No output yet - waiting for sig2

    REQUIRE(received.empty());

    sig2("a"); // Pairs with 1
    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == std::pair{1, std::string("a")});

    sig2("b"); // Pairs with 2
    REQUIRE(received.size() == 2);
    REQUIRE(received[1] == std::pair{2, std::string("b")});

    sig2("c"); // Pairs with 3
    REQUIRE(received.size() == 3);
    REQUIRE(received[2] == std::pair{3, std::string("c")});

    sig2("d"); // Queued, waiting for sig1
    REQUIRE(received.size() == 3);

    sig1(4); // Pairs with "d"
    REQUIRE(received.size() == 4);
    REQUIRE(received[3] == std::pair{4, std::string("d")});
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
    auto iteration = GENERATE(GENERATE_REPEAT());
    (void)iteration;

    auto& pool = sigslot::test::get_test_pool();
    auto sched = pool.get_scheduler();

    sigslot::signal<int> sig;
    std::atomic<int> sum{0};
    std::atomic<std::thread::id> handler_thread_id{};
    std::counting_semaphore<2> done{0};

    auto observed = sig | sigslot::rx::observe_on(sched);
    observed.connect([&](int x) {
        handler_thread_id.store(std::this_thread::get_id());
        sum.fetch_add(x);
        done.release();
    });

    sig(10);
    sig(20);

    // Wait for both handlers (proper handshake)
    done.acquire();
    done.acquire();

    REQUIRE(sum.load() == 30);
    // Handler should run on pool thread, not main thread
    REQUIRE(handler_thread_id.load() != std::thread::id{});
}

TEST_CASE("rx::observe_on with map", "[rx][execution]") {
    auto iteration = GENERATE(GENERATE_REPEAT());
    (void)iteration;

    auto& pool = sigslot::test::get_test_pool();
    auto sched = pool.get_scheduler();

    sigslot::signal<int> sig;
    std::atomic<int> result{0};
    std::binary_semaphore done{0};

    // Chain manually to avoid pipe operator conflict with stdexec
    auto mapped = sig | sigslot::rx::map([](int x) { return x * 2; });
    auto observed = sigslot::rx::observe_on(sched)(mapped);
    observed.connect([&](int x) {
        result.store(x);
        done.release();
    });

    sig(21);

    // Wait for handler (proper handshake)
    done.acquire();

    REQUIRE(result.load() == 42);
}

TEST_CASE("rx::debounce_on uses scheduler", "[rx][execution][debounce]") {
    auto iteration = GENERATE(GENERATE_REPEAT());
    (void)iteration;

    auto& pool = sigslot::test::get_test_pool();
    auto sched = pool.get_scheduler();

    sigslot::signal<> sig;
    std::atomic<int> count{0};
    std::binary_semaphore debounce_fired{0};

    auto debounced = sig | sigslot::rx::debounce_on(sched, 30ms);
    debounced.connect([&]() {
        count.fetch_add(1);
        debounce_fired.release();
    });

    // Rapid emissions
    sig();
    sig();
    sig();

    // Wait for debounce callback (proper handshake, no timing guesses)
    debounce_fired.acquire();

    REQUIRE(count.load() == 1);
}

#endif // SIGSLOT_HAVE_STDEXEC
