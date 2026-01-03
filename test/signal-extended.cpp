// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: palacaze/sigslot contributors


#include <catch2/catch_test_macros.hpp>
#include "support/signal-matchers.hpp"

using namespace sigslot::matchers;
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <sigslot/signal.hpp>

static int sum = 0;

void f(sigslot::connection& c, int i) {
    sum += i;
    c.disconnect();
}

struct s {
    static void sf(sigslot::connection& c, int i) {
        sum += i;
        c.disconnect();
    }
    void f(sigslot::connection& c, int i) {
        sum += i;
        c.disconnect();
    }
};

struct o {
    void operator()(sigslot::connection& c, int i) {
        sum += i;
        c.disconnect();
    }
};

TEST_CASE("Free Connection", "[signal_extended]") {
    sum = 0;
    sigslot::signal<int> sig;
    sig.connect_extended(f);

    sig(1);
    REQUIRE(sum == 1);
    sig(1);
    REQUIRE(sum == 1);
}

TEST_CASE("Static Connection", "[signal_extended]") {
    sum = 0;
    sigslot::signal<int> sig;
    sig.connect_extended(&s::sf);

    sig(1);
    REQUIRE(sum == 1);
    sig(1);
    REQUIRE(sum == 1);
}

TEST_CASE("Pmf Connection", "[signal_extended]") {
    sum = 0;
    sigslot::signal<int> sig;
    s p;
    sig.connect_extended(&s::f, &p);

    sig(1);
    REQUIRE(sum == 1);
    sig(1);
    REQUIRE(sum == 1);
}

TEST_CASE("Function Object Connection", "[signal_extended]") {
    sum = 0;
    sigslot::signal<int> sig;
    sig.connect_extended(o{});

    sig(1);
    REQUIRE(sum == 1);
    sig(1);
    REQUIRE(sum == 1);
}

TEST_CASE("Lambda Connection", "[signal_extended]") {
    sum = 0;
    sigslot::signal<int> sig;

    sig.connect_extended([&](sigslot::connection& c, int i) {
        sum += i;
        c.disconnect();
    });
    sig(1);
    REQUIRE(sum == 1);

    sig.connect_extended([&](sigslot::connection& c, int i) mutable {
        sum += 2 * i;
        c.disconnect();
    });
    sig(1);
    REQUIRE(sum == 3);
    sig(1);
    REQUIRE(sum == 3);
}
