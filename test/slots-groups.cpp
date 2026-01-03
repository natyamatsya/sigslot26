#include <catch2/catch_test_macros.hpp>
#include "support/signal-matchers.hpp"

using namespace sigslot::matchers;
#include <catch2/matchers/catch_matchers_range_equals.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <sigslot/signal.hpp>
#include <algorithm>
#include <array>
#include <random>


namespace {
constexpr size_t NUM_GROUPS = 100;
constexpr size_t NUM_SLOTS = 1000;
} // namespace


using res_container = std::vector<int32_t>;


static auto pusher(int position) {
    return [pos = std::move(position)](res_container& c) { c.push_back(pos); };
}

static auto adder(int value) {
    return [v = std::move(value)](int& s) { s += v; };
}

TEST_CASE("Random Groups", "[slots_groups]") {
    res_container results;
    sigslot::signal<res_container&> sig;

    std::mt19937_64 gen{std::random_device()()};

    // create N groups with random ids
    std::uniform_int_distribution<int> dist(std::numeric_limits<int>::lowest());
    std::array<int32_t, NUM_GROUPS> gids;
    std::generate_n(gids.begin(), NUM_GROUPS, [&] { return dist(gen); });

    // create
    std::uniform_int_distribution<size_t> slots_dist{0, NUM_GROUPS - 1};

    for (size_t i = 0; i < NUM_SLOTS; ++i) {
        auto gid = gids[slots_dist(gen)];
        sig.connect(pusher(gid), gid);
    }

    // signal
    sig(results);

    // check that the resulting container is sorted
    REQUIRE(std::is_sorted(results.begin(), results.end()));
}

TEST_CASE("Disconnect Group", "[slots_groups]") {
    int sum = 0;
    sigslot::signal<int&> sig;
    sig.connect(adder(3), 3);
    sig.connect(adder(1), 1);
    sig.connect(adder(2), 2);

    sig(sum);
    REQUIRE(sum == 6);

    sig.disconnect(2);
    sig(sum);
    REQUIRE(sum == 10);
}

TEST_CASE("Block Group", "[slots_groups]") {
    int sum = 0;
    sigslot::signal<int&> sig;
    sig.connect(adder(3), 3);
    sig.connect(adder(1), 1);
    sig.connect(adder(2), 2);

    sig(sum);
    REQUIRE(sum == 6);

    sig.block(3);
    sig(sum);
    REQUIRE(sum == 9);

    sig.unblock(3);
    sig(sum);
    REQUIRE(sum == 15);
}
