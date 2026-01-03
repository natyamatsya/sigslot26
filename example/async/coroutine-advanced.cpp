/**
 * @brief Advanced coroutine support example for sigslot26
 * 
 * Demonstrates async range operations with signals:
 * - take(N) to collect N emissions
 * - filter() for conditional processing
 * - transform() for mapping values
 * - Composable async operations
 * 
 @note These examples use synchronous signal emission for clarity.
 * In real applications, signals would be emitted from event loops,
 * user interactions, or async I/O operations.
 */

#include <sigslot/signal.hpp>
#include <sigslot/async/async.hpp>
#include <print>
#include <vector>

// Example 1: Take N emissions
void example_take() {
    std::println("Example 1: Take first N emissions");
    std::println("-----------------------------------");

    sigslot::signal<int, std::string> sig;

    // Start a coroutine that will collect first 3 emissions
    auto collect_three = [&sig]() -> sigslot::async::task<void> {
        std::println("Collecting first 3 emissions...");
        auto awaitable = sigslot::async::make_awaitable(sig);

        for (auto [num, str] : awaitable.take(3)) {
            std::println("  Received: {} - {}", num, str);
        }

        std::println("Collection complete!\n");
        co_return;
    };

    auto task = collect_three();

    // Emit signals - the coroutine will process first 3 and stop
    std::println("Emitting 5 signals...");
    sig(1, "first");
    sig(2, "second");
    sig(3, "third");
    sig(4, "fourth"); // This won't be processed
    sig(5, "fifth");  // This won't be processed
}

// Example 2: Filter emissions
void example_filter() {
    std::println("\nExample 2: Filter emissions");
    std::println("----------------------------");

    sigslot::signal<int> sig;

    auto filter_evens = [&sig]() -> sigslot::async::task<void> {
        std::println("Filtering for even numbers (will collect 3)...");
        auto awaitable = sigslot::async::make_awaitable(sig);

        int count = 0;
        for (auto [value] : awaitable.filter([](int x) { return x % 2 == 0; })) {
            std::println("  Even number: {}", value);
            if (++count >= 3)
                break;
        }

        std::println("Filter complete!\n");
        co_return;
    };

    auto task = filter_evens();

    // Emit mixed odd/even numbers
    std::println("Emitting numbers 1-10...");
    for (int i = 1; i <= 10; ++i) {
        sig(i);
    }
}

// Example 3: Transform emissions
void example_transform() {
    std::println("\nExample 3: Transform emissions");
    std::println("-------------------------------");

    sigslot::signal<int> sig;

    auto double_values = [&sig]() -> sigslot::async::task<void> {
        std::println("Transforming values (x * 2), collecting 4...");
        auto awaitable = sigslot::async::make_awaitable(sig);

        int count = 0;
        for (auto doubled : awaitable.transform([](int x) { return x * 2; })) {
            std::println("  {} -> {}", count + 1, doubled);
            if (++count >= 4)
                break;
        }

        std::println("Transform complete!\n");
        co_return;
    };

    auto task = double_values();

    std::println("Emitting values 10, 20, 30, 40, 50...");
    sig(10);
    sig(20);
    sig(30);
    sig(40);
    sig(50);
}

// Example 4: Composable pipeline
void example_pipeline() {
    std::println("\nExample 4: Composable pipeline");
    std::println("--------------------------------");

    sigslot::signal<int> sig;

    auto process_pipeline = [&sig]() -> sigslot::async::task<void> {
        std::println("Pipeline: filter multiples of 3, collect 3...");
        auto awaitable = sigslot::async::make_awaitable(sig);

        // Filter for multiples of 3
        auto filtered = awaitable.filter([](int x) { return x % 3 == 0; });

        int count = 0;
        for (auto [value] : filtered) {
            std::println("  Multiple of 3: {}", value);
            if (++count >= 3)
                break;
        }

        std::println("Pipeline complete!\n");
        co_return;
    };

    auto task = process_pipeline();

    std::println("Emitting numbers 1-15...");
    for (int i = 1; i <= 15; ++i) {
        sig(i);
    }
}

// Example 5: Practical use case - collecting results
void example_practical() {
    std::println("\nExample 5: Practical use case - collecting results");
    std::println("---------------------------------------------------");

    sigslot::signal<int> sig;

    auto collect_results = [&sig]() -> sigslot::async::task<std::vector<int>> {
        std::println("Collecting first 5 values into a vector...");
        auto awaitable = sigslot::async::make_awaitable(sig);

        std::vector<int> results;
        for (auto [value] : awaitable.take(5)) {
            results.push_back(value);
            std::println("  Collected: {}", value);
        }

        co_return results;
    };

    auto task = collect_results();

    std::println("Emitting values...");
    for (int i = 100; i <= 110; ++i) {
        sig(i);
    }

    // In a real application, you'd await the task result
    std::println("Collection complete!");
    std::println("(In production, use task.get() to retrieve the vector)\n");
}

int main() {
    std::println("=== Advanced Coroutine Examples ===\n");

    example_take();
    example_filter();
    example_transform();
    example_pipeline();
    example_practical();

    std::println("=== All examples completed ===");
    return 0;
}
