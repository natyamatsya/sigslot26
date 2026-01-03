// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: palacaze/sigslot contributors

#include <sigslot/signal.hpp>
#include <print>
#include <string>

struct foo {
    // Notice how we accept a double as first argument here
    // This is fine because float is convertible to double
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void bar(float d, int i, bool b, std::string& s) {
        s = b ? std::to_string(i) : std::to_string(d);
    }
};

// Function objects can cope with default arguments and overloading.
// It does not work with static and member functions.
struct obj {
    void operator()(float /*unused*/, int /*unused*/, bool /*unused*/, std::string& /*unused*/,
                    int /*unused*/ = 0) {
        std::println("I was here");
    }

    void operator()() {}
};

// A generic function object that deals with any input argument
struct printer {
    template<typename T, typename... Ts>
    void operator()(T a, Ts&&... args) const {
        std::print("{}", a);
        ((std::print(" {}", std::forward<Ts>(args))), ...);
        std::println("");
    }
};

int main() {
    // declare a signal with float, int, bool and string& arguments
    sigslot::signal<float, int, bool, std::string&> sig;

    // a Generic lambda that prints its arguments to stdout
    auto lambda_printer = [](auto a, auto&&... args) static {
        std::print("{}", a);
        ((std::print(" {}", args)), ...);
        std::println("");
    };

    // connect the slots
    foo ff;
    sig.connect(printer());
    sig.connect(&foo::bar, &ff);
    sig.connect(lambda_printer);
    sig.connect(obj());

    float f = 1.F;
    int16_t i = 2;
    std::string s = "0";

    // emit a signal
    sig(f, i, false, s);
    sig(f, i, true, s);

    return 0;
}
