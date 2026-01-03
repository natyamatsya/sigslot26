/**
 * @brief Basic coroutine support example for sigslot26
 * 
 * Demonstrates C++20 coroutine integration with signals:
 * - Awaitable signals (co_await)
 * - Timeout support
 * - Async task patterns
 */

#include <sigslot/signal.hpp>
#include <sigslot/async/async.hpp>
#include <print>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// Example 1: Basic awaitable signal
auto wait_for_signal(sigslot::signal<int>& sig) -> sigslot::async::task<void> {
    std::println("Waiting for signal...");

    auto awaitable = sigslot::async::make_awaitable(sig);
    auto [value] = co_await awaitable;

    std::println("Received value: {}", value);
}

// Example 2: Signal with timeout
auto wait_with_timeout(sigslot::signal<int>& sig) -> sigslot::async::task<void> {
    std::println("Waiting for signal with 200ms timeout...");

    auto result = co_await sigslot::async::await_with_timeout(sig, 200ms);

    if (result) {
        auto [value] = *result;
        std::println("Received value: {}", value);
    } else {
        std::println("Timeout! No signal received");
    }
}

// Example 3: Multiple awaits
auto wait_multiple(sigslot::signal<int, std::string>& sig) -> sigslot::async::task<void> {
    std::println("\nWaiting for 3 signals...");

    for (int i = 0; i < 3; ++i) {
        auto awaitable = sigslot::async::make_awaitable(sig);
        auto [num, str] = co_await awaitable;
        std::println("  Signal {}: {} - {}", i + 1, num, str);
    }

    std::println("Received all 3 signals!");
}

int main() {
    std::println("=== Coroutine Basic Examples ===\n");

    // Example 1: Basic await
    {
        std::println("Example 1: Basic await");
        sigslot::signal<int> sig;

        auto task = wait_for_signal(sig);

        // Emit signal
        std::this_thread::sleep_for(50ms);
        sig(42);

        std::this_thread::sleep_for(100ms);
    }

    // Example 2: Timeout
    {
        std::println("\nExample 2: Timeout");
        sigslot::signal<int> sig;

        // This will timeout
        auto task1 = wait_with_timeout(sig);
        std::this_thread::sleep_for(300ms);

        // This will receive the signal
        auto task2 = wait_with_timeout(sig);
        std::this_thread::sleep_for(50ms);
        sig(100);
        std::this_thread::sleep_for(100ms);
    }

    // Example 3: Multiple awaits
    {
        std::println("\nExample 3: Multiple awaits");
        sigslot::signal<int, std::string> sig;

        auto task = wait_multiple(sig);

        // Emit signals
        std::this_thread::sleep_for(50ms);
        sig(1, "first");
        std::this_thread::sleep_for(50ms);
        sig(2, "second");
        std::this_thread::sleep_for(50ms);
        sig(3, "third");

        std::this_thread::sleep_for(100ms);
    }

    // Example 4: Coroutine slots (template-based storage already supports this!)
    {
        std::println("\nExample 4: Coroutine slots");
        sigslot::signal<int> sig;

        // Connect lambda that returns a task
        sig.connect([](int x) -> sigslot::async::task<void> {
            std::println("  Processing {} in coroutine...", x);
            std::this_thread::sleep_for(50ms);
            std::println("  Done processing {}", x);
            co_return;
        });

        sig(10);
        sig(20);

        std::this_thread::sleep_for(200ms);
    }

    std::println("\n=== All examples completed ===");
    return 0;
}
