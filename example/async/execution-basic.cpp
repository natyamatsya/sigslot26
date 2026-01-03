/**
 * @file execution-basic.cpp
 * @brief Example demonstrating std::execution integration with sigslot
 * 
 * This example shows how to use signals with the sender/receiver model
 * from P2300 (std::execution) via NVIDIA's stdexec library.
 * 
 * Build with: -DSIGSLOT_ENABLE_STDEXEC=ON
 */

#include <sigslot/signal.hpp>
#include <sigslot/async/execution.hpp>
#include <print>
#include <thread>
#include <chrono>

#if SIGSLOT_EXECUTION_AVAILABLE

#include <exec/static_thread_pool.hpp>

int main() {
    std::println("=== sigslot + std::execution example ===\n");

    // Example 1: Basic signal as sender
    {
        std::println("1. Basic signal as sender:");

        sigslot::signal<int, std::string> sig;

        // Create a sender from the signal
        auto sender = sigslot::async::as_sender(sig);

        // Emit in a separate thread
        std::thread emitter([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::println("   Emitting signal...");
            sig(42, "hello");
        });

        // Wait for emission using sync_wait
        auto result = stdexec::sync_wait(std::move(sender));
        if (result) {
            auto [num, str] = *result;
            std::println("   Received: {}, {}", num, str);
        }

        emitter.join();
    }

    // Example 2: Composing with then()
    {
        std::println("\n2. Composing with then():");

        sigslot::signal<int> sig;

        // Chain transformations
        auto sender = sigslot::async::as_sender(sig) | stdexec::then([](int x) {
                          std::println("   Transform: {} -> {}", x, x * 2);
                          return x * 2;
                      }) |
                      stdexec::then([](int x) {
                          std::println("   Transform: {} -> {}", x, x + 10);
                          return x + 10;
                      });

        std::thread emitter([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            sig(5);
        });

        auto result = stdexec::sync_wait(std::move(sender));
        if (result) {
            auto [value] = *result;
            std::println("   Final result: {}", value);
        }

        emitter.join();
    }

    // Example 3: Execute slot on thread pool
    {
        std::println("\n3. Execute slot on thread pool:");

        exec::static_thread_pool pool(4);
        auto sched = pool.get_scheduler();

        sigslot::signal<int> sig;
        int processed = 0;

        // Connect slot that executes on thread pool
        auto conn = sigslot::async::connect_on(sig, sched, [&](int x) {
            std::println("   Processing {} on pool thread", x);
            processed = x * 2;
        });

        sig(21);

        // Give the pool time to process
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::println("   Processed result: {}", processed);

        conn.disconnect();
    }

    std::println("\n=== Done ===");
    return 0;
}

#else // !SIGSLOT_EXECUTION_AVAILABLE

int main() {
    std::println("std::execution support is not enabled.");
    std::println("Build with -DSIGSLOT_ENABLE_STDEXEC=ON to enable.");
    return 0;
}

#endif
