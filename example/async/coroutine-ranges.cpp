/**
 * @brief Async range operations example for sigslot26
 * 
 * Demonstrates how to use async range operations with signals.
 * 
 @note This example shows the API design. The current implementation
 * has limitations with synchronous signal emission patterns due to
 * coroutine suspension timing. These operations work best with:
 * - Event loops
 * - Async I/O callbacks
 * - Timer-based emissions
 * - User interaction handlers
 */

#include <sigslot/signal.hpp>
#include <sigslot/async/async.hpp>
#include <print>

// Example 1: Basic take() usage
void example_take_api() {
    std::println("Example 1: take(N) API");
    std::println("----------------------");
    std::println("Collect exactly N emissions from a signal:\n");

    std::println("  sigslot::signal<int> sig;");
    std::println("  auto awaitable = sigslot::async::make_awaitable(sig);");
    std::println("");
    std::println("  // In a coroutine:");
    std::println("  for (auto [value] : awaitable.take(5)) {{");
    std::println("      process(value);  // Called exactly 5 times");
    std::println("  }}");
    std::println("");
}

// Example 2: Filter API
void example_filter_api() {
    std::println("\nExample 2: filter() API");
    std::println("-----------------------");
    std::println("Process only emissions that match a predicate:\n");

    std::println("  sigslot::signal<int> sig;");
    std::println("  auto awaitable = sigslot::async::make_awaitable(sig);");
    std::println("");
    std::println("  // Filter for even numbers:");
    std::println("  for (auto [value] : awaitable.filter([](int x) {{ return x % 2 == 0; }})) {{");
    std::println("      process_even(value);  // Only called for even values");
    std::println("      if (done) break;");
    std::println("  }}");
    std::println("");
}

// Example 3: Transform API
void example_transform_api() {
    std::println("\nExample 3: transform() API");
    std::println("--------------------------");
    std::println("Map signal values to different types:\n");

    std::println("  sigslot::signal<int> sig;");
    std::println("  auto awaitable = sigslot::async::make_awaitable(sig);");
    std::println("");
    std::println("  // Double each value:");
    std::println("  for (auto doubled : awaitable.transform([](int x) {{ return x * 2; }})) {{");
    std::println("      process(doubled);  // Receives transformed values");
    std::println("      if (done) break;");
    std::println("  }}");
    std::println("");
}

// Example 4: Composable pipelines
void example_pipeline_api() {
    std::println("\nExample 4: Composable pipelines");
    std::println("--------------------------------");
    std::println("Chain multiple operations together:\n");

    std::println("  sigslot::signal<int> sig;");
    std::println("  auto awaitable = sigslot::async::make_awaitable(sig);");
    std::println("");
    std::println("  // Filter then collect:");
    std::println("  auto evens = awaitable.filter([](int x) {{ return x % 2 == 0; }});");
    std::println("  for (auto [value] : evens) {{");
    std::println("      process(value);");
    std::println("      if (count++ >= 10) break;");
    std::println("  }}");
    std::println("");
}

// Example 5: Practical use cases
void example_use_cases() {
    std::println("\nExample 5: Practical use cases");
    std::println("-------------------------------");
    std::println("");

    std::println("Use case 1: Event stream processing");
    std::println("  - Filter UI events by type");
    std::println("  - Transform mouse coordinates");
    std::println("  - Take first N matching events");
    std::println("");

    std::println("Use case 2: Async data collection");
    std::println("  - Collect sensor readings until threshold");
    std::println("  - Filter out noise/invalid readings");
    std::println("  - Transform raw values to calibrated units");
    std::println("");

    std::println("Use case 3: Network message handling");
    std::println("  - Filter messages by type or priority");
    std::println("  - Transform protocol messages to domain objects");
    std::println("  - Take messages until connection closes");
    std::println("");
}

// Example 6: Implementation notes
void example_notes() {
    std::println("\nImplementation Notes");
    std::println("--------------------");
    std::println("");
    std::println("Current limitations:");
    std::println("  - Generators start executing immediately on creation");
    std::println("  - Works best with async event sources (timers, I/O, UI events)");
    std::println("  - Synchronous emission patterns may cause timing issues");
    std::println("");
    std::println("Best practices:");
    std::println("  - Use with event loops or async frameworks");
    std::println("  - Emit signals from callbacks, not inline");
    std::println("  - Always use break conditions to exit infinite streams");
    std::println("  - Consider using take(N) for bounded collection");
    std::println("");
    std::println("Future enhancements:");
    std::println("  - Integration with std::execution (C++26)");
    std::println("  - Scheduler-based emission");
    std::println("  - Backpressure support");
    std::println("  - Cancellation tokens");
    std::println("");
}

int main() {
    std::println("=== Async Range Operations API Guide ===\n");

    example_take_api();
    example_filter_api();
    example_transform_api();
    example_pipeline_api();
    example_use_cases();
    example_notes();

    std::println("=== Guide complete ===");
    std::println("\nFor working examples, see:");
    std::println("  - coroutine-basic.cpp (awaitable signals)");
    std::println("  - Integration with async frameworks");

    return 0;
}
