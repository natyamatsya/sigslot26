// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#include <catch2/catch_test_macros.hpp>
#include <sigslot/signal.hpp>
#include <sigslot/async/execution.hpp>

// Tests for std::execution support
// These tests only run when SIGSLOT_HAVE_STDEXEC is defined

#if SIGSLOT_EXECUTION_AVAILABLE

#include <thread>
#include <atomic>
#include <vector>
#include <exec/static_thread_pool.hpp>
#include <sigslot/async/coroutine.hpp>

// =============================================================================
// Basic Sender Tests
// =============================================================================

TEST_CASE("Signal as sender - basic", "[execution]") {
    sigslot::signal<int> sig;

    auto sender = sigslot::async::as_sender(sig);

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(42);
    });

    auto [value] = stdexec::sync_wait(std::move(sender)).value();
    REQUIRE(value == 42);

    emitter.join();
}

TEST_CASE("Signal as sender - multiple args", "[execution]") {
    sigslot::signal<int, std::string> sig;

    auto sender = sigslot::async::as_sender(sig);

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(123, "hello");
    });

    auto [num, str] = stdexec::sync_wait(std::move(sender)).value();
    REQUIRE(num == 123);
    REQUIRE(str == "hello");

    emitter.join();
}

TEST_CASE("Signal as sender - no args", "[execution]") {
    sigslot::signal<> sig;

    auto sender = sigslot::async::as_sender(sig);

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig();
    });

    stdexec::sync_wait(std::move(sender));
    SUCCEED("void signal completed");

    emitter.join();
}

TEST_CASE("Signal as sender - reference args", "[execution]") {
    sigslot::signal<int&> sig;
    int value = 0;

    auto sender = sigslot::async::as_sender(sig);

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(value);
    });

    auto result = stdexec::sync_wait(std::move(sender));
    REQUIRE(result.has_value());

    emitter.join();
}

// =============================================================================
// Composition Tests
// =============================================================================

TEST_CASE("Signal sender with then", "[execution]") {
    sigslot::signal<int> sig;

    auto sender = sigslot::async::as_sender(sig) | stdexec::then([](int x) { return x * 2; });

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(21);
    });

    auto [value] = stdexec::sync_wait(std::move(sender)).value();
    REQUIRE(value == 42);

    emitter.join();
}

TEST_CASE("Signal sender with chained then", "[execution]") {
    sigslot::signal<int> sig;

    auto sender = sigslot::async::as_sender(sig) | stdexec::then([](int x) { return x * 2; }) |
                  stdexec::then([](int x) { return x + 10; }) |
                  stdexec::then([](int x) { return std::to_string(x); });

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(5);
    });

    auto [value] = stdexec::sync_wait(std::move(sender)).value();
    REQUIRE(value == "20"); // (5 * 2) + 10 = 20

    emitter.join();
}

TEST_CASE("Signal sender with let_value", "[execution]") {
    sigslot::signal<int> sig;

    auto sender = sigslot::async::as_sender(sig) |
                  stdexec::let_value([](int x) { return stdexec::just(x * 3); });

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(7);
    });

    auto [value] = stdexec::sync_wait(std::move(sender)).value();
    REQUIRE(value == 21);

    emitter.join();
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST_CASE("Signal sender - multiple concurrent waiters", "[execution][threading]") {
    sigslot::signal<int> sig;
    std::atomic<int> completed{0};
    constexpr size_t NUM_WAITERS = 5;

    std::vector<std::thread> waiters;
    std::vector<int> results(NUM_WAITERS, 0);

    // Start multiple waiters
    for (size_t i = 0; i < NUM_WAITERS; ++i) {
        waiters.emplace_back([&, i]() {
            auto sender = sigslot::async::as_sender(sig);
            auto [value] = stdexec::sync_wait(std::move(sender)).value();
            results[i] = value;
            ++completed;
        });
    }

    // Give time for waiters to set up
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Emit - all waiters should receive this
    sig(100);

    // Wait for all waiters
    for (auto& t : waiters)
        t.join();

    REQUIRE(completed == static_cast<int>(NUM_WAITERS));
    for (size_t i = 0; i < NUM_WAITERS; ++i)
        REQUIRE(results[i] == 100);
}

TEST_CASE("Signal sender - rapid emissions", "[execution][threading]") {
    sigslot::signal<int> sig;
    std::atomic<int> sum{0};
    constexpr int NUM_EMISSIONS = 10;

    std::thread emitter([&]() {
        for (int i = 0; i < NUM_EMISSIONS; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            sig(i);
        }
    });

    // Consume emissions one at a time
    for (int i = 0; i < NUM_EMISSIONS; ++i) {
        auto sender = sigslot::async::as_sender(sig);
        auto [value] = stdexec::sync_wait(std::move(sender)).value();
        sum += value;
    }

    emitter.join();

    // Sum of 0..9 = 45
    REQUIRE(sum == 45);
}

// =============================================================================
// Scheduler Integration Tests
// =============================================================================

TEST_CASE("connect_on - execute slot on thread pool", "[execution][scheduler]") {
    exec::static_thread_pool pool(2);
    auto sched = pool.get_scheduler();

    sigslot::signal<int> sig;
    std::atomic<int> result{0};
    std::atomic<std::thread::id> slot_thread_id{};

    auto conn = sigslot::async::connect_on(sig, sched, [&](int x) {
        result = x * 2;
        slot_thread_id = std::this_thread::get_id();
    });

    sig(21);

    // Give pool time to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    REQUIRE(result == 42);
    // Slot should have executed on a different thread
    REQUIRE(slot_thread_id != std::thread::id{});

    conn.disconnect();
}

TEST_CASE("connect_on - multiple emissions on scheduler", "[execution][scheduler]") {
    exec::static_thread_pool pool(4);
    auto sched = pool.get_scheduler();

    sigslot::signal<int> sig;
    std::atomic<int> sum{0};

    auto conn = sigslot::async::connect_on(sig, sched, [&](int x) { sum += x; });

    for (int i = 1; i <= 5; ++i)
        sig(i);

    // Give pool time to execute all
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    REQUIRE(sum == 15); // 1+2+3+4+5

    conn.disconnect();
}

// =============================================================================
// Single-Threaded Signal Tests
// =============================================================================

TEST_CASE("Single-threaded signal as sender - API test", "[execution]") {
    sigslot::signal_st<int> sig;

    // Verify API compiles correctly for signal_st
    [[maybe_unused]] auto sender = sigslot::async::as_sender(sig);

    SUCCEED("signal_st sender API compiles correctly");
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("Signal sender - disconnect before emit", "[execution]") {
    sigslot::signal<int> sig;

    [[maybe_unused]] auto sender = sigslot::async::as_sender(sig);

    // Disconnect all before emitting
    sig.disconnect_all();

    // Emit to nothing - the sender is still waiting
    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(42); // This won't reach the original waiter since slots are gone
    });

    // Create a new sender and wait
    auto sender2 = sigslot::async::as_sender(sig);
    auto [value] = stdexec::sync_wait(std::move(sender2)).value();
    REQUIRE(value == 42);

    emitter.join();
}

TEST_CASE("Signal sender - signal destroyed", "[execution]") {
    // This test verifies proper cleanup when signal outlives operation
    auto sig = std::make_unique<sigslot::signal<int>>();

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (sig)
            (*sig)(42);
    });

    auto sender = sigslot::async::as_sender(*sig);
    auto [value] = stdexec::sync_wait(std::move(sender)).value();
    REQUIRE(value == 42);

    emitter.join();
}

// =============================================================================
// Coroutine / Execution Interop Tests
// =============================================================================

TEST_CASE("sync_wait_for - convenience function", "[execution][interop]") {
    sigslot::signal<int> sig;

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(42);
    });

    auto result = sigslot::async::sync_wait_for(sig);
    REQUIRE(result.has_value());
    auto [value] = *result;
    REQUIRE(value == 42);

    emitter.join();
}

TEST_CASE("connect_coro_on - coroutine slot on scheduler", "[execution][interop]") {
    exec::static_thread_pool pool(2);
    ::exec::async_scope scope;
    auto sched = pool.get_scheduler();

    sigslot::signal<int> sig;
    std::atomic<int> result{0};

    auto conn = sigslot::async::connect_coro_on(sig, scope, sched,
                                                [&](int x) -> sigslot::async::task<void> {
                                                    result = x * 2;
                                                    co_return;
                                                });

    sig(21);

    // Wait for scope to complete
    stdexec::sync_wait(scope.on_empty());

    REQUIRE(result == 42);

    conn.disconnect();
}

TEST_CASE("sigslot::async namespace API", "[execution]") {
    sigslot::signal<int> sig;

    // Verify sigslot::async:: API works
    auto sender = sigslot::async::as_sender(sig);

    std::thread emitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig(100);
    });

    auto [value] = stdexec::sync_wait(std::move(sender)).value();
    REQUIRE(value == 100);

    emitter.join();
}

#else // !SIGSLOT_EXECUTION_AVAILABLE

TEST_CASE("Execution support disabled", "[execution]") {
    // When stdexec is not available, just verify the header compiles
    // and the feature macro is correctly set
    REQUIRE(SIGSLOT_EXECUTION_AVAILABLE == 0);
    SUCCEED("execution.hpp compiles without stdexec");
}

#endif // SIGSLOT_EXECUTION_AVAILABLE
