// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: palacaze/sigslot contributors


#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <sigslot/signal.hpp>

template<typename T>
struct object {
    object(T i)
        : v{i} {}

    void inc_val(const T& i) {
        if (i != v) {
            v++;
            sig(v);
        }
    }

    void dec_val(const T& i) {
        if (i != v) {
            v--;
            sig(v);
        }
    }

    T v;
    sigslot::signal<T> sig;
};

TEST_CASE("Recursive", "[recursive]") {
    object<int> i1(-1);
    object<int> i2(10);

    i1.sig.connect(&object<int>::dec_val, &i2);
    i2.sig.connect(&object<int>::inc_val, &i1);

    i1.inc_val(0);

    REQUIRE(i1.v == i2.v);
}

TEST_CASE("Self Recursive", "[recursive]") {
    int i = 0;

    sigslot::signal<int> s;
    s.connect([&](int v) {
        if (i < 10) {
            i++;
            s(v + 1);
        }
    });

    s(0);

    REQUIRE(i == 10);
}
