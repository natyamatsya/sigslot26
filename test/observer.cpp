#include <catch2/catch_test_macros.hpp>
#include "support/signal-matchers.hpp"

using namespace sigslot::matchers;
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <sigslot/signal.hpp>

#include <list>
#include <memory>
#include <vector>

struct s : ::sigslot::observer {
    ~s() override { this->disconnect_all(); }

    void f1(int& i) { ++i; }
};

struct s_st : ::sigslot::observer_st {
    void f1(int& i) { ++i; }
};

struct s_plain {
    void f1(int& i) { ++i; }
};

template<typename T, template<typename...> class SIG_T>
void test_observer() {
    SIG_T<int&> sig;

    // Automatic disconnect via observer inheritance
    {
        T p1;
        sig.connect(&T::f1, &p1);
        REQUIRE_THAT(sig, HasSlots(1));
        REQUIRE_THAT(sig, IsConnectedTo(&p1));

        {
            T p2;
            sig.connect(&T::f1, &p2);
            REQUIRE_THAT(sig, HasSlots(2));
            REQUIRE_THAT(sig, IsConnectedTo(&p1));
            REQUIRE_THAT(sig, IsConnectedTo(&p2));
        }

        REQUIRE_THAT(sig, HasSlots(1));
        REQUIRE_THAT(sig, IsConnectedTo(&p1));
    }

    REQUIRE_THAT(sig, HasNoSlots());

    // No automatic disconnect
    {
        s_plain p;
        sig.connect(&s_plain::f1, &p);
        REQUIRE_THAT(sig, HasSlots(1));
    }

    REQUIRE_THAT(sig, HasSlots(1));
}

template<typename T, template<typename...> class SIG_T>
void test_observer_signals() {
    int sum = 0;
    SIG_T<int&> sig;

    {
        T p1;
        sig.connect(&T::f1, &p1);
        sig(sum);
        REQUIRE(sum == 1);
        {
            T p2;
            sig.connect(&T::f1, &p2);
            sig(sum);
            REQUIRE(sum == 3);
        }
        sig(sum);
        REQUIRE(sum == 4);
    }

    sig(sum);
    REQUIRE(sum == 4);
}

template<typename T, template<typename...> class SIG_T>
void test_observer_signals_shared() {
    int sum = 0;
    SIG_T<int&> sig;

    {
        auto p1 = std::make_shared<T>();
        sig.connect(&T::f1, p1);
        sig(sum);
        REQUIRE(sum == 1);
        {
            auto p2 = std::make_shared<T>();
            sig.connect(&T::f1, p2);
            sig(sum);
            REQUIRE(sum == 3);
        }
        sig(sum);
        REQUIRE(sum == 4);
    }

    sig(sum);
    REQUIRE(sum == 4);
}

template<typename T, template<typename...> class SIG_T>
void test_observer_signals_list() {
    int sum = 0;
    SIG_T<int&> sig;

    {
        std::list<T> l;
        for (auto i = 0; i < 10; ++i) {
            l.emplace_back();
            sig.connect(&T::f1, &l.back());
        }

        REQUIRE_THAT(sig, HasSlots(10));
        sig(sum);
        REQUIRE(sum == 10);
    }

    REQUIRE_THAT(sig, HasNoSlots());
    sig(sum);
    REQUIRE(sum == 10);
}

template<typename T, template<typename...> class SIG_T>
void test_observer_signals_vector() {
    int sum = 0;
    SIG_T<int&> sig;

    {
        std::vector<std::unique_ptr<T>> v;
        for (auto i = 0; i < 10; ++i) {
            v.emplace_back(new T);
            sig.connect(&T::f1, v.back().get());
        }
        REQUIRE_THAT(sig, HasSlots(10));
        sig(sum);
        REQUIRE(sum == 10);
    }

    REQUIRE_THAT(sig, HasNoSlots());
    sig(sum);
    REQUIRE(sum == 10);
}

TEST_CASE("Observer thread-safe", "[observer]") {
    test_observer<s, sigslot::signal>();
}

TEST_CASE("Observer single-threaded", "[observer]") {
    test_observer<s_st, sigslot::signal_st>();
}

TEST_CASE("Observer signals thread-safe", "[observer]") {
    test_observer_signals<s, sigslot::signal>();
}

TEST_CASE("Observer signals single-threaded", "[observer]") {
    test_observer_signals<s_st, sigslot::signal_st>();
}

TEST_CASE("Observer signals shared thread-safe", "[observer]") {
    test_observer_signals_shared<s, sigslot::signal>();
}

TEST_CASE("Observer signals shared single-threaded", "[observer]") {
    test_observer_signals_shared<s_st, sigslot::signal_st>();
}

TEST_CASE("Observer signals list thread-safe", "[observer]") {
    test_observer_signals_list<s, sigslot::signal>();
}

TEST_CASE("Observer signals list single-threaded", "[observer]") {
    test_observer_signals_list<s_st, sigslot::signal_st>();
}

TEST_CASE("Observer signals vector thread-safe", "[observer]") {
    test_observer_signals_vector<s, sigslot::signal>();
}

TEST_CASE("Observer signals vector single-threaded", "[observer]") {
    test_observer_signals_vector<s_st, sigslot::signal_st>();
}
