// Phase 6.2 Tests: slot_variant unit tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <sigslot/slot-variant.hpp>
#include <memory>
#include <string>

using namespace sigslot::detail;

// Test fixtures
struct Counter {
    int count = 0;
    void increment() { ++count; }
    void add(int n) { count += n; }
    void add_ref(int& n) { count += n; }
};

TEST_CASE("slot_variant basic construction", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    SECTION("default construction creates empty slot") {
        slot_t slot;
        REQUIRE(slot.empty());
        REQUIRE(slot.tag() == slot_tag::empty);
        REQUIRE_FALSE(slot.connected());
    }

    SECTION("make_plain creates connected slot") {
        auto slot = slot_t::make_plain([](int) {}, int32_t{0});
        REQUIRE_FALSE(slot.empty());
        REQUIRE(slot.tag() == slot_tag::plain);
        REQUIRE(slot.connected());
        REQUIRE_FALSE(slot.blocked());
    }

    SECTION("make_pmf creates connected slot") {
        Counter counter;
        auto slot = slot_t::make_pmf(&Counter::add, &counter, int32_t{0});
        REQUIRE_FALSE(slot.empty());
        REQUIRE(slot.tag() == slot_tag::pmf);
        REQUIRE(slot.connected());
    }
}

TEST_CASE("slot_variant plain slot invocation", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    SECTION("lambda without capture") {
        int result = 0;
        auto slot = slot_t::make_plain([&result](int x) { result = x; }, int32_t{0});

        REQUIRE(slot(42));
        REQUIRE(result == 42);
    }

    SECTION("lambda with capture") {
        int multiplier = 2;
        int result = 0;
        auto slot = slot_t::make_plain([&result, multiplier](int x) { result = x * multiplier; },
                                       int32_t{0});

        REQUIRE(slot(21));
        REQUIRE(result == 42);
    }

    SECTION("free function") {
        static int static_result = 0;
        auto slot = slot_t::make_plain([](int x) { static_result = x; }, int32_t{0});

        REQUIRE(slot(100));
        REQUIRE(static_result == 100);
    }
}

TEST_CASE("slot_variant PMF invocation", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    SECTION("member function with raw pointer") {
        Counter counter;
        auto slot = slot_t::make_pmf(&Counter::add, &counter, int32_t{0});

        REQUIRE(slot(5));
        REQUIRE(counter.count == 5);

        REQUIRE(slot(3));
        REQUIRE(counter.count == 8);
    }
}

TEST_CASE("slot_variant tracked slot", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    SECTION("tracked slot with valid weak_ptr") {
        auto counter = std::make_shared<Counter>();
        std::weak_ptr<Counter> weak = counter;

        auto slot = slot_t::make_pmf_tracked(&Counter::add, weak, int32_t{0});

        REQUIRE(slot.connected());
        REQUIRE(slot(10));
        REQUIRE(counter->count == 10);
    }

    SECTION("tracked slot with expired weak_ptr") {
        std::weak_ptr<Counter> weak;
        {
            auto counter = std::make_shared<Counter>();
            weak = counter;
        }
        // counter is now destroyed, weak_ptr is expired

        auto slot = slot_t::make_pmf_tracked(&Counter::add, weak, int32_t{0});

        // Call should fail (return false) because tracked object is gone
        REQUIRE_FALSE(slot(10));
    }
}

TEST_CASE("slot_variant blocking", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    int result = 0;
    auto slot = slot_t::make_plain([&result](int x) { result = x; }, int32_t{0});

    SECTION("unblocked slot is called") {
        REQUIRE_FALSE(slot.blocked());
        REQUIRE(slot(42));
        REQUIRE(result == 42);
    }

    SECTION("blocked slot is not called") {
        slot.block();
        REQUIRE(slot.blocked());
        REQUIRE_FALSE(slot(99));
        REQUIRE(result == 0); // Not modified
    }

    SECTION("unblock allows calls again") {
        slot.block();
        slot.unblock();
        REQUIRE_FALSE(slot.blocked());
        REQUIRE(slot(42));
        REQUIRE(result == 42);
    }
}

TEST_CASE("slot_variant disconnection", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    int result = 0;
    auto slot = slot_t::make_plain([&result](int x) { result = x; }, int32_t{0});

    SECTION("connected slot is called") {
        REQUIRE(slot.connected());
        REQUIRE(slot(42));
        REQUIRE(result == 42);
    }

    SECTION("disconnected slot is not called") {
        REQUIRE(slot.disconnect()); // Returns true (was connected)
        REQUIRE_FALSE(slot.connected());
        REQUIRE_FALSE(slot(99));
        REQUIRE(result == 0); // Not modified
    }

    SECTION("disconnect returns false if already disconnected") {
        slot.disconnect();
        REQUIRE_FALSE(slot.disconnect()); // Already disconnected
    }
}

TEST_CASE("slot_variant group and index", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    auto slot = slot_t::make_plain([](int) {}, int32_t{42});

    SECTION("group is set correctly") {
        REQUIRE(slot.group() == 42);
    }

    SECTION("index can be set and retrieved") {
        slot.set_index(100);
        REQUIRE(slot.index() == 100);

        slot.set_index(999);
        REQUIRE(slot.index() == 999);
    }
}

TEST_CASE("slot_variant move semantics", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    int result = 0;
    auto slot1 = slot_t::make_plain([&result](int x) { result = x; }, int32_t{5});
    slot1.set_index(42);

    SECTION("move construction") {
        slot_t slot2(std::move(slot1));

        REQUIRE(slot1.empty());
        REQUIRE_FALSE(slot2.empty());
        REQUIRE(slot2.tag() == slot_tag::plain);
        REQUIRE(slot2.group() == 5);
        REQUIRE(slot2.index() == 42);
        REQUIRE(slot2(100));
        REQUIRE(result == 100);
    }

    SECTION("move assignment") {
        slot_t slot2;
        slot2 = std::move(slot1);

        REQUIRE(slot1.empty());
        REQUIRE_FALSE(slot2.empty());
        REQUIRE(slot2(200));
        REQUIRE(result == 200);
    }
}

TEST_CASE("slot_variant multiple argument types", "[slot_variant]") {
    SECTION("no arguments") {
        using slot_t = slot_variant<int32_t>;
        int called = 0;
        auto slot = slot_t::make_plain([&called]() { ++called; }, int32_t{0});

        REQUIRE(slot());
        REQUIRE(called == 1);
    }

    SECTION("multiple arguments") {
        using slot_t = slot_variant<int32_t, int, double, const std::string&>;
        std::string captured;
        auto slot = slot_t::make_plain(
            [&captured](int a, double b, const std::string& c) {
                captured = std::to_string(a) + "," + std::to_string(b) + "," + c;
            },
            int32_t{0});

        REQUIRE(slot(1, 2.5, std::string("hello")));
        REQUIRE(captured == "1,2.500000,hello");
    }
}

TEST_CASE("slot_variant size constraints", "[slot_variant]") {
    using slot_t = slot_variant<int32_t, int>;

    // Verify the slot_variant has reasonable size
    // Should be around 96-128 bytes (storage + metadata)
    REQUIRE(sizeof(slot_t) <= 256);
    REQUIRE(sizeof(slot_t) >= 64); // At least storage size

    INFO("sizeof(slot_variant<int32_t, int>) = " << sizeof(slot_t));
}
