// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors


#include <catch2/catch_test_macros.hpp>
#include <sigslot/signal.hpp>
#include <print>

#include <iostream>
#include <vector>

TEST_CASE("Signal Performance", "[signal_performance]") {
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    constexpr int count = 1000;
    double ref_ns = 0.;
    sigslot::signal<> sig;
    {
        std::vector<sigslot::scoped_connection> connections;
        connections.reserve(count);
        for (int i = 0; i < count; i++) {
            connections.emplace_back(sig.connect([] {}));
        }

        // Measure first signal time as reference
        const TimePoint begin = Clock::now();
        sig();
        ref_ns = double(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
    }

    // Measure signal after all slot were disconnected
    const TimePoint begin = Clock::now();
    sig();
    const double after_disconnection_ns =
        double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());

    // Ensure that the signal cost is not > at 10%
    const auto max_delta = 0.1;
    const auto delta = (after_disconnection_ns - ref_ns) / ref_ns;

    std::println("ref: {} us", ref_ns / 1000);
    std::println("after: {} us", after_disconnection_ns / 1000);
    std::println("delta: {} SU", delta);

    REQUIRE(delta < max_delta);
}
