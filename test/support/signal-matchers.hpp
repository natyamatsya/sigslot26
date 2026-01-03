#pragma once

#include <catch2/matchers/catch_matchers_templated.hpp>
#include <string>

namespace sigslot::matchers {

// Matcher for signal slot counts
struct HasSlotsMatcher : Catch::Matchers::MatcherGenericBase {
    size_t expected;

    explicit HasSlotsMatcher(size_t count)
        : expected(count) {}

    template<typename Signal>
    bool match(Signal& sig) const {
        return sig.slot_count() == expected;
    }

    std::string describe() const override { return "has " + std::to_string(expected) + " slot(s)"; }
};

inline HasSlotsMatcher HasSlots(size_t count) {
    return HasSlotsMatcher(count);
}

// Matcher to check if a signal has no slots
struct IsEmptyMatcher : Catch::Matchers::MatcherGenericBase {
    template<typename Signal>
    bool match(Signal& sig) const {
        return sig.slot_count() == 0;
    }

    std::string describe() const override { return "has no slots"; }
};

inline IsEmptyMatcher HasNoSlots() {
    return IsEmptyMatcher();
}

// Matcher to check if a signal has at least N slots
struct HasAtLeastSlotsMatcher : Catch::Matchers::MatcherGenericBase {
    size_t minimum;

    explicit HasAtLeastSlotsMatcher(size_t count)
        : minimum(count) {}

    template<typename Signal>
    bool match(Signal& sig) const {
        return sig.slot_count() >= minimum;
    }

    std::string describe() const override {
        return "has at least " + std::to_string(minimum) + " slot(s)";
    }
};

inline HasAtLeastSlotsMatcher HasAtLeastSlots(size_t count) {
    return HasAtLeastSlotsMatcher(count);
}

// Matcher to check if a connection is valid/connected
struct IsConnectedMatcher : Catch::Matchers::MatcherGenericBase {
    template<typename Connection>
    bool match(Connection const& conn) const {
        return conn.valid();
    }

    std::string describe() const override { return "is connected"; }
};

inline IsConnectedMatcher IsConnected() {
    return IsConnectedMatcher();
}

// Matcher to check if a connection is disconnected
struct IsDisconnectedMatcher : Catch::Matchers::MatcherGenericBase {
    template<typename Connection>
    bool match(Connection const& conn) const {
        return !conn.valid();
    }

    std::string describe() const override { return "is disconnected"; }
};

inline IsDisconnectedMatcher IsDisconnected() {
    return IsDisconnectedMatcher();
}

// Matcher to check if a callable is connected to a signal
template<typename Callable>
struct IsConnectedToCallableMatcher : Catch::Matchers::MatcherGenericBase {
    const Callable& callable;

    explicit IsConnectedToCallableMatcher(const Callable& c)
        : callable(c) {}

    template<typename Signal>
    bool match(Signal& sig) const {
        return sig.is_connected(callable);
    }

    std::string describe() const override { return "is connected to callable"; }
};

template<typename Callable>
IsConnectedToCallableMatcher<Callable> IsConnectedTo(const Callable& c) {
    return IsConnectedToCallableMatcher<Callable>(c);
}

// Matcher to check if an object is connected to a signal
template<typename Obj>
struct IsConnectedToObjectMatcher : Catch::Matchers::MatcherGenericBase {
    const Obj* obj;

    explicit IsConnectedToObjectMatcher(const Obj* o)
        : obj(o) {}

    template<typename Signal>
    bool match(Signal& sig) const {
        return sig.is_connected(obj);
    }

    std::string describe() const override { return "is connected to object"; }
};

template<typename Obj>
IsConnectedToObjectMatcher<Obj> IsConnectedTo(const Obj* o) {
    return IsConnectedToObjectMatcher<Obj>(o);
}

// Matcher to check if callable+object pair is connected
template<typename Callable, typename Obj>
struct IsConnectedToPairMatcher : Catch::Matchers::MatcherGenericBase {
    const Callable& callable;
    const Obj* obj;

    IsConnectedToPairMatcher(const Callable& c, const Obj* o)
        : callable(c)
        , obj(o) {}

    template<typename Signal>
    bool match(Signal& sig) const {
        return sig.is_connected(callable, obj);
    }

    std::string describe() const override { return "is connected to callable+object"; }
};

template<typename Callable, typename Obj>
IsConnectedToPairMatcher<Callable, Obj> IsConnectedTo(const Callable& c, const Obj* o) {
    return IsConnectedToPairMatcher<Callable, Obj>(c, o);
}

} // namespace sigslot::matchers

// Usage:
// #include "support/signal-matchers.hpp"
// using namespace sigslot::matchers;
//
// Signal slot count matchers:
//   REQUIRE_THAT(sig, HasSlots(3));
//   REQUIRE_THAT(sig, HasNoSlots());
//   REQUIRE_THAT(sig, HasAtLeastSlots(1));
//
// Connection state matchers:
//   REQUIRE_THAT(conn, IsConnected());
//   REQUIRE_THAT(conn, IsDisconnected());
//
// Signal connection graph matchers:
//   REQUIRE_THAT(sig, IsConnectedTo(callable));
//   REQUIRE_THAT(sig, IsConnectedTo(&object));
//   REQUIRE_THAT(sig, IsConnectedTo(&Class::method, &object));
