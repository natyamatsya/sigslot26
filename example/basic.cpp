#include <sigslot/signal.hpp>
#include <print>

void f() {
    std::println("free function");
}

class s {
public:
    void m() const { std::println("member function: v={}", v); }
    static void sm() { std::println("static member function"); }

private:
    const int v{123};
};

struct o {
    void operator()() { std::println("function object"); }
};

int main() {
    s d;
    auto lambda = []() static { std::println("lambda"); };

    // declare a signal instance with no arguments
    sigslot::signal<> sig;

    // sigslot::signal will connect to any callable provided it has compatible
    // arguments. Here are diverse examples
    sig.connect(f);
    sig.connect(&s::m, &d);
    sig.connect(&s::sm);
    sig.connect(o());
    sig.connect(lambda);

    // Avoid hitting bug https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68071
    // on old GCC compilers
#ifndef __clang__
#if GCC_VERSION > 70300
    auto gen_lambda = [](auto&&... /*a*/) static { std::println("generic lambda"); };
    sig.connect(gen_lambda);
#endif
#endif

    // emit a signal
    sig();

    return 0;
}
