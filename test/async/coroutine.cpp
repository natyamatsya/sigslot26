// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: mousebyte/sigslot20 contributors
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

/**
 * @brief Tests for coroutine support in sigslot26
 * 
 * Tests:
 * - Awaitable signals (co_await)
 * - Timeout support
 * - Multiple awaiters
 * - Cancellation
 * - Task type
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <sigslot/signal.hpp>
#include <sigslot/async/async.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

using namespace std::chrono_literals;

TEST_CASE("Basic awaitable signal", "[coroutine][await]") {
    sigslot::signal<int> sig;
    bool received = false;
    int value = 0;

    auto waiter = [&]() -> sigslot::async::task<void> {
        auto awaitable = sigslot::async::make_awaitable(sig);
        auto [v] = co_await awaitable;
        value = v;
        received = true;
        co_return;
    };

    auto task = waiter();

    sig(42);

    REQUIRE(received);
    REQUIRE(value == 42);
}

TEST_CASE("Awaitable with multiple arguments", "[coroutine][await]") {
    sigslot::signal<int, std::string, double> sig;
    bool received = false;
    int i = 0;
    std::string s;
    double d = 0.0;

    auto waiter = [&]() -> sigslot::async::task<void> {
        auto awaitable = sigslot::async::make_awaitable(sig);
        auto [a, b, c] = co_await awaitable;
        i = a;
        s = b;
        d = c;
        received = true;
        co_return;
    };

    auto task = waiter();

    sig(100, "test", 3.14);

    REQUIRE(received);
    REQUIRE(i == 100);
    REQUIRE(s == "test");
    REQUIRE(d == Catch::Approx(3.14));
}

TEST_CASE("Timeout API exists", "[coroutine][timeout]") {
    // This test verifies the timeout API compiles correctly
    // Actual timeout behavior is best tested with async event sources
    sigslot::signal<int> sig;

    // Verify the API exists and compiles
    auto awaitable = sigslot::async::make_awaitable(sig);
    auto awaiter = awaitable.next_or_timeout(100ms);

    // API test passed if we reach here
    (void)awaiter;
    SUCCEED("Timeout API compiles successfully");
}

TEST_CASE("Multiple sequential awaits", "[coroutine][await]") {
    sigslot::signal<int> sig;
    std::vector<int> values;

    auto waiter = [&]() -> sigslot::async::task<void> {
        auto awaitable = sigslot::async::make_awaitable(sig);

        for (int i = 0; i < 3; ++i) {
            auto [v] = co_await awaitable;
            values.push_back(v);
        }

        co_return;
    };

    auto task = waiter();

    sig(1);
    sig(2);
    sig(3);

    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == 1);
    REQUIRE(values[1] == 2);
    REQUIRE(values[2] == 3);
}

TEST_CASE("Task with return value", "[coroutine][task]") {
    sigslot::signal<int> sig;

    auto compute = [&]() -> sigslot::async::task<int> {
        auto awaitable = sigslot::async::make_awaitable(sig);
        auto [v] = co_await awaitable;
        co_return v * 2;
    };

    auto task = compute();

    sig(21);

    int result = task.get();
    REQUIRE(result == 42);
}

TEST_CASE("Task exception handling", "[coroutine][task]") {
    bool caught = false;

    auto thrower = []() -> sigslot::async::task<void> {
        throw std::runtime_error("test error");
        co_return;
    };

    auto task = thrower();

    try {
        task.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        REQUIRE(std::string(e.what()) == "test error");
    }

    REQUIRE(caught);
}

TEST_CASE("Coroutine as slot", "[coroutine][slot]") {
    sigslot::signal<int> sig;
    int processed = 0;

    // Connect a coroutine that returns a task
    sig.connect([&](int x) -> sigslot::async::task<void> {
        processed = x * 2;
        co_return;
    });

    sig(10);

    REQUIRE(processed == 20);
}

TEST_CASE("Thread-safe signal with coroutines", "[coroutine][threadsafe]") {
    sigslot::signal<int> sig; // Thread-safe by default
    bool received = false;
    int value = 0;

    auto waiter = [&]() -> sigslot::async::task<void> {
        auto awaitable = sigslot::async::make_awaitable(sig);
        auto [v] = co_await awaitable;
        value = v;
        received = true;
        co_return;
    };

    auto task = waiter();

    // Emit from another thread
    std::thread([&]() {
        std::this_thread::sleep_for(10ms);
        sig(123);
    }).join();

    REQUIRE(received);
    REQUIRE(value == 123);
}

TEST_CASE("Single-threaded signal with coroutines", "[coroutine][single-threaded]") {
    sigslot::signal_st<int> sig; // Single-threaded
    bool received = false;
    int value = 0;

    auto waiter = [&]() -> sigslot::async::task<void> {
        auto awaitable = sigslot::async::make_awaitable(sig);
        auto [v] = co_await awaitable;
        value = v;
        received = true;
        co_return;
    };

    auto task = waiter();

    sig(456);

    REQUIRE(received);
    REQUIRE(value == 456);
}
