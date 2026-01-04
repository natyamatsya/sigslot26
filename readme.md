# Sigslot, a signal-slot library

> **⚠️ EXPERIMENTAL - NOT PRODUCTION READY**
>
> Sigslot26 is an experimental C++23 migration of sigslot. This project is exploring modern C++23 features and is **not recommended for production use**. The library is under active development and APIs may change without notice.

Sigslot is a header-only, thread safe implementation of signal-slots for C++.

This fork (`sigslot26`) builds on [palacaze/sigslot](https://github.com/palacaze/sigslot) and [mousebyte/sigslot20](https://github.com/mousebyte/sigslot20), adding C++23 support and async integration.

## Changes from upstream

### Async & Reactive
- **C++23 requirement** - Uses `std::print`, deducing this, `std::expected`, and other C++23 features
- **std::execution (P2300) integration** - Signals can be used as senders via `sigslot::async::as_sender()`
- **Coroutine support** - Signals are awaitable with `co_await sigslot::async::make_awaitable(sig)`
- **Reactive extensions** - `rx::map`, `rx::filter`, `rx::throttle`, `rx::debounce`, `rx::distinct`, `rx::scan`, `rx::buffer`, `rx::take`, `rx::skip`, `rx::merge`, `rx::combine_latest`, `rx::zip`, `rx::observe_on`
- **Qt async adapters** - `connect_on_event_loop()`, `connect_on_thread()`, `as_qfuture()`

### High-Performance Signal Variants
- **`signal_inline`** - Non-thread-safe signal with inline `slot_variant` storage for ~60% faster emission
- **`signal_inline_rw`** - Thread-safe variant using read-write locks
- **`signal_inline_rcu`** - Lock-free variant using RCU (Read-Copy-Update) pattern
- **`signal_inline_seqlock`** - Lock-free variant using seqlock with fixed-capacity storage (fastest thread-safe option)

### Performance Optimizations
- **`slot_variant`** - Inline function pointer dispatch eliminates virtual call overhead
- **Fixed-capacity `fixed_vector`** - Thread-safe container with atomic size for seqlock pattern
- **Cache line padding** - Configurable via `SIGSLOT_CACHE_LINE_PADDING` to reduce false sharing
- **Index caching** - Configurable via `SIGSLOT_INDEX_CACHING` to reduce atomic loads on hot path
- **Arena allocator** - Bump-pointer allocation for slot objects (64KB chunks)

### Infrastructure
- **Catch2 test framework** - Replaces assert-based tests with Catch2
- **CI improvements** - Multi-platform builds with sanitizer coverage (ASan, TSan, UBSan)
- **Comprehensive benchmarks** - Google Benchmark integration for performance analysis

## High-Performance Signal Variants

For performance-critical scenarios, `sigslot26` provides specialized signal implementations in `<sigslot/signal-inline.hpp>`:

```cpp
#include <sigslot/signal-inline.hpp>

// Non-thread-safe, fastest option (~60% faster than signal_base)
sigslot::signal_inline<int> sig;
sig.connect([](int x) { /* ... */ });
sig(42);

// Thread-safe with read-write lock
sigslot::signal_inline_rw<int> sig_rw;
sig_rw.connect([](int x) { /* ... */ });
sig_rw(42);  // Safe from multiple threads

// Lock-free with RCU pattern
sigslot::signal_inline_rcu<int> sig_rcu;
sig_rcu.connect([](int x) { /* ... */ });
sig_rcu(42);  // Lock-free emission

// Lock-free with seqlock (fastest thread-safe, fixed capacity)
sigslot::signal_inline_seqlock<16, int> sig_seq;  // Max 16 slots
auto result = sig_seq.connect([](int x) { /* ... */ });
if (!result) {
    // Handle capacity exceeded: result.error() == seqlock_error::capacity_exceeded
}
sig_seq(42);  // Lock-free emission, wait-free when no writer active
```

### Performance Comparison

| Signal Type | Single Slot | Thread Safety | Notes |
|-------------|-------------|---------------|-------|
| `signal_inline` | 4.5 ns | ❌ None | Fastest, single-threaded only |
| `signal_inline_seqlock` | 4.7 ns | ✅ Lock-free | Fixed capacity, retry on conflict |
| `signal_inline_rw` | 7.6 ns | ✅ RW-lock | Unbounded, read-heavy workloads |
| `signal_inline_rcu` | 9.9 ns | ✅ Lock-free | Unbounded, copy-on-write |
| `signal_base` (default) | 10.6 ns | ✅ Full | Full features, connection objects |

## Features

The main goal was to replace Boost.Signals2.

Apart from the usual features, it offers

- Thread safety,
- Object lifetime tracking for automatic slot disconnection (extensible through ADL),
- RAII connection management,
- Slot groups to enforce slots execution order,
- Reasonable performance. and a simple and straightforward implementation.

Sigslot is unit-tested and should be reliable and stable enough to replace Boost Signals2.

The tests run cleanly under the address, thread and undefined behaviour sanitizers.

### CI Matrix and Sanitizer Configuration

The library is continuously tested across multiple platforms and compilers:

| Platform | Compiler | Sanitizers | Notes |
|----------|----------|------------|-------|
| Windows | MSVC | ASan, UBSan | Address + Undefined Behavior |
| Windows | clang-cl | TSan, UBSan | Thread + Undefined Behavior |
| Ubuntu | GCC 14 | TSan, UBSan | Thread + Undefined Behavior |
| Ubuntu | Clang 18 | TSan | Thread only* |
| macOS | Apple Clang | TSan | Thread only* |

*\*Note: UBSan is disabled for Clang builds when using stdexec integration. Combining TSan and UBSan with Clang causes hangs in stdexec's `sync_wait` operations. GCC does not exhibit this issue.*

Many implementations allow signal return types, Sigslot does not because I have
no use for them. If I can be convinced of otherwise I may change my mind later on.

## Installation

No compilation or installation is required, just include `sigslot/signal.hpp`
and use it. This fork requires a C++23 compliant compiler. It is tested with
GCC 14, Clang 18, MSVC 2022, and Apple Clang on macOS.

However, be aware of a potential gotcha on Windows with MSVC and Clang-Cl compilers,
which may need the `/OPT:NOICF` linker flags in exceptional situations. Read The
Implementation Details chapter for an explanation.

A CMake list file is supplied for installation purpose and generating a CMake import
module. This is the preferred installation method. The `Pal::Sigslot` imported target
is available and already applies the needed linker flags. It is also required for
examples and tests, which optionally depend on Qt5 and Boost for adapters unit tests.

```cmake
# Using Sigslot from cmake
find_package(PalSigslot)

add_executable(MyExe main.cpp)
target_link_libraries(MyExe PRIVATE Pal::Sigslot)
```

A configuration option `SIGSLOT_REDUCE_COMPILE_TIME` is available at configuration
time. When activated, it attempts to reduce code bloat by avoiding heavy template
instantiations resulting from calls to `std::make_shared`.
This option is off by default, but can be activated for those who wish to favor
code size and compilation time at the expanse of slightly less efficient code.

Installation may be done using the following instructions from the root directory:

```sh
mkdir build && cd build
cmake .. -DSIGSLOT_REDUCE_COMPILE_TIME=ON -DCMAKE_INSTALL_PREFIX=~/local
cmake --build . --target install

# If you want to compile examples:
cmake --build . --target sigslot-examples

# And compile/execute unit tests:
cmake --build . --target sigslot-tests
```

### CMake FetchContent

`Pal::Sigslot` can also be integrated using the [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) method.

```cmake
include(FetchContent)

FetchContent_Declare(
  sigslot
  GIT_REPOSITORY https://github.com/natyamatsya/sigslot26
  GIT_TAG        develop
)
FetchContent_MakeAvailable(sigslot)

add_executable(MyExe main.cpp)
target_link_libraries(MyExe PRIVATE Pal::Sigslot)
```

## Demos

The `example/async/` folder contains stress test demos showcasing async and reactive features:

### HFT Reactive Demo (`async/reactive-demo.cpp`)

A high-frequency trading simulation demonstrating reactive extensions. Simulates multiple stock exchanges emitting rapid price ticks with a shared market factor creating realistic correlation (~0.85) between stocks.

```bash
# Build and run
cmake --build build --target async-reactive-demo
./build/example/async-reactive-demo [stdexec|threads] [num_exchanges] [duration_seconds]

# Examples
./build/example/async-reactive-demo              # Default: hardware_concurrency exchanges, 10s
./build/example/async-reactive-demo 32 5         # 32 exchanges, 5 seconds
./build/example/async-reactive-demo stdexec 64 10  # All 64 companies, stdexec mode
```

**Reactive operators demonstrated:** `throttle`, `distinct`, `filter`, `scan`, `map`, `combine_latest`, `merge`

### Monte Carlo Stress Test (`async/stress-test.cpp`)

A parallel Monte Carlo π estimation demonstrating thread safety and connection management under high contention. Tests signal emission, slot invocation, and connection churn across multiple threads.

```bash
cmake --build build --target async-stress-test
./build/example/async-stress-test [stdexec|threads|coroutines] [num_workers] [samples_per_worker]
```

> **Note:** These demos are for illustration purposes only and do not aim for production-grade performance requirements.

## Documentation

Sigslot implements the signal-slot construct popular in UI frameworks, making it
easy to use the observer pattern or event-based programming. The main entry point
of the library is the `sigslot::signal<T...>` class template.

A signal is an object that can emit typed notifications, really values parametrized
after the signal class template parameters, and register any number of notification
handlers (callables) of compatible argument types to be executed with the values
supplied whenever a signal emission happens. In signal-slot parlance this is called
connecting a slot to a signal, where a "slot" represents a callable instance and
a "connection" can be thought of as a conceptual link from signal to slot.

All the snippets presented below are available in compilable source code form in
the example subdirectory.

### Basic usage

Here is a first example that showcases the most basic features of the library.

We first declare a parameter-free signal `sig`, then we proceed to connect several
slots and at last emit a signal which triggers the invocation of every slot callable
connected beforehand. Notice how The library handles diverse forms of callables.

```cpp
#include <sigslot/signal.hpp>
#include <print>

void f() { std::println("free function"); }

struct s {
    void m() { std::println("member function"); }
    static void sm() { std::println("static member function"); }
};

struct o {
    void operator()() { std::println("function object"); }
};

int main() {
    s d;
    auto lambda = []() { std::println("lambda"); };
    auto gen_lambda = [](auto&&...) { std::println("generic lambda"); };

    // declare a signal instance with no arguments
    sigslot::signal<> sig;

    // connect slots
    sig.connect(f);
    sig.connect(&s::m, &d);
    sig.connect(&s::sm);
    sig.connect(o());
    sig.connect(lambda);
    sig.connect(gen_lambda);

    // a free connect() function is also available
    sigslot::connect(sig, f);

    // emit a signal
    sig();
}
```

By default, the slot invocation order when emitting a signal is unspecified, please
do not rely on it being always the same. You may constrain a particular invocation
order by using slot groups, which are presented later on.

### Signal with arguments

That first example was simple but not so useful, let us move on to a signal that
emits values instead. A signal can emit any number of arguments, below.

```cpp
#include <sigslot/signal.hpp>
#include <print>
#include <string>

struct foo {
    // Notice how we accept a double as first argument here.
    // This is fine because float is convertible to double.
    // 's' is a reference and can thus be modified.
    void bar(double d, int i, bool b, std::string& s) {
        s = b ? std::to_string(i) : std::to_string(d);
    }
};

// Function objects can cope with default arguments and overloading.
// It does not work with static and member functions.
struct obj {
    void operator()(float, int, bool, std::string&, int = 0) {
        std::println("I was here");
    }

    void operator()() {}
};

int main() {
    // declare a signal with float, int, bool and string& arguments
    sigslot::signal<float, int, bool, std::string&> sig;

    // a generic lambda that prints its arguments to stdout
    auto printer = [](auto a, auto&&... args) {
        std::println("{}", a);
        (std::println("{}", args), ...);
    };

    // connect the slots
    foo ff;
    sig.connect(printer);
    sig.connect(&foo::bar, &ff);
    sig.connect(obj());

    float f = 1.f;
    short i = 2;  // convertible to int
    std::string s = "0";

    // emit a signal
    sig(f, i, false, s);
    sig(f, i, true, s);
}
```

As shown, slots arguments types don't need to be strictly identical to the signal
template parameters, being convertible-from is fine. Generic arguments are fine too,
as shown with the `printer` generic lambda (which could have been written as a
function template too).

Right now there are two limitations that I can think of with respect to callable
handling: default arguments and function overloading. Both are working correctly
in the case of function objects but will fail to compile with static and member
functions, for different but related reasons.

#### Coping with overloaded functions

Consider the following piece of code:

```cpp
struct foo {
    void bar(double d);
    void bar();
};
```

What should `&foo::bar` refer to? As per overloading, this pointer over member
function does not map to a unique symbol, so the compiler won't be able to pick
the right symbol. One way of resolving the right symbol is to explicitly cast the
function pointer to the right function type. Here is an example that does just that
using a little helper tool for a lighter syntax (In fact I will probably add this
to the library soon).

```cpp
#include <sigslot/signal.hpp>

template <typename... Args, typename C>
constexpr auto overload(void (C::*ptr)(Args...)) {
    return ptr;
}

template <typename... Args>
constexpr auto overload(void (*ptr)(Args...)) {
    return ptr;
}

struct obj {
    void operator()(int) const {}
    void operator()() {}
};

struct foo {
    void bar(int) {}
    void bar() {}

    static void baz(int) {}
    static void baz() {}
};

void moo(int) {}
void moo() {}

int main() {
    sigslot::signal<int> sig;

    // connect the slots, casting to the right overload if necessary
    foo ff;
    sig.connect(overload<int>(&foo::bar), &ff);
    sig.connect(overload<int>(&foo::baz));
    sig.connect(overload<int>(&moo));
    sig.connect(obj());

    sig(0);

    return 0;
}
```

#### Coping with function with default arguments

Default arguments are not part of the function type signature, and can be redefined,
so they are really difficult to deal with. When connecting a slot to a signal, the
library determines if the supplied callable can be invoked with the signal argument
types, but at this point the existence of default function arguments is unknown
so there might be a mismatch in the number of arguments.

A simple work around for this use case would is to create a bind adapter, in fact
we can even make it quite generic like so:

```cpp
#include <sigslot/signal.hpp>

#define ADAPT(func) \
    [=](auto && ...a) { (func)(std::forward<decltype(a)>(a)...); }

void foo(int &i, int b = 1) {
    i += b;
}

int main() {
    int i = 0;

    // fine, all the arguments are handled
    sigslot::signal<int&, int> sig1;
    sig1.connect(foo);
    sig1(i, 2);

    // must wrap in an adapter
    i = 0;
    sigslot::signal<int&> sig2;
    sig2.connect(ADAPT(foo));
    sig2(i);

    return 0;
}
```

### Connection management

#### Connection object

What was not made apparent until now is that `signal::connect()` actually returns
a `sigslot::connection` object that may be used to manage the behaviour and lifetime
of a signal-slot connection. `sigslot::connection` is a lightweight object (basically
a `std::weak_ptr`) that allows interaction with an ongoing signal-slot connection
and exposes the following features:

- Status querying, that is testing whether a connection is valid, ongoing or facing destruction,
- Connection (un)blocking, which allows to temporarily disable the invocation of a slot when a signal is emitted,
- Disconnection of a slot, the destruction of a connection previously created via `signal::connect()`.

A `sigslot::connection` does not tie a connection to a scope: this is not a RAII
object, which explains why it can be copied. It can be however implicitly converted
into a `sigslot::scoped_connection` which destroys the connection when going out
of scope.

Here is an example illustrating some of those features:

```cpp
#include <sigslot/signal.hpp>
#include <string>

int i = 0;

void f() { i += 1; }

int main() {
    sigslot::signal<> sig;

    // keep a sigslot::connection object
    auto c1 = sig.connect(f);

    // disconnection
    sig();  // i == 1
    c1.disconnect();
    sig();  // i == 1

    // scope based disconnection
    {
        sigslot::scoped_connection sc = sig.connect(f);
        sig();  // i == 2
    }

    sig();  // i == 2;


    // connection blocking
    auto c2 = sig.connect(f);
    sig();  // i == 3
    c2.block();
    sig();  // i == 3
    c2.unblock();
    sig();  // i == 4
}
```

#### Extended connection signature

Sigslot supports an extended slot signature with an additional `sigslot::connection`
reference as first argument, which permits connection management from inside the
slot. This extended signature is accessible using the `connect_extended()` method.

```cpp
#include <sigslot/signal.hpp>

int main() {
    int i = 0;
    sigslot::signal<> sig;

    // extended connection
    auto f = [](auto &con) {
        i += 1;             // do work
        con.disconnect();   // then disconnects
    };

    sig.connect_extended(f);
    sig();  // i == 1
    sig();  // i == 1 because f was disconnected
}
```

#### Automatic slot lifetime tracking

The user must make sure that the lifetime of a slot exceeds the one of a signal,
which may get tedious in complex software. To simplify this task, Sigslot can
automatically disconnect slot object whose lifetime it is able to track. In order
to do that, the slot must be convertible to a weak pointer of some form.

`std::shared_ptr` and `std::weak_ptr` are supported out of the box, and adapters
are provided to support `boost::shared_ptr`, `boost::weak_ptr` and Qt `QSharedPointer`,
`QWeakPointer` and any class deriving from `QObject`.

Other trackable objects can be added by declaring a `to_weak()` adapter function.

```cpp
#include <sigslot/signal.hpp>
#include <sigslot/adapter/qt.hpp>

int sum = 0;

struct s {
    void f(int i) { sum += i; }
};

class MyObject : public QObject {
    Q_OBJECT
public:
    void add(int i) const { sum += i; }
};

int main() {
    sum = 0;
    signal<int> sig;

    // track lifetime of object and also connect to a member function
    auto p = std::make_shared<s>();
    sig.connect(&s::f, p);

    sig(1);     // sum == 1
    p.reset();
    sig(1);     // sum == 1

    // track an unrelated object lifetime
    struct dummy;
    auto l = [&](int i) { sum += i; };

    auto d = std::make_shared<dummy>();
    sig.connect(l, d);
    sig(1);     // sum == 2
    d.reset();
    sig(1);     // sum == 2

    // track a QObject
    {
        MyObject o;
        sig.connect(&MyObject::add, &o);

        sig(1); // sum == 3
    }

    sig(1);     // sum == 3
}
```

#### Intrusive slot lifetime tracking

Another way of ensuring automatic disconnection of pointer over member functions
slots is by explicitly inheriting from `sigslot::observer` or `sigslot::observer_st`.
The former is thread-safe, contrary to the later.

Here is an example usage.

```cpp
#include <sigslot/signal.hpp>

int sum = 0;

struct s : sigslot::observer_st {
    void f(int i) { sum += i; }
};

struct s_mt : sigslot::observer {
    ~s_mt() {
        // Needed to ensure proper disconnection prior to object destruction
        // in multithreaded contexts.
        this->disconnect_all();
    }

    void f(int i) { sum += i; }
};

int main() {
    sum = 0;
    signal<int> sig;

    {
        // Lifetime of object instance p is tracked
        s p;
        s_mt pm;
        sig.connect(&s::f, &p);
        sig.connect(&s_mt::f, &pm);
        sig(1);     // sum == 2
    }

    // The slots got disconnected at instance destruction
    sig(1);         // sum == 2
}
```

The objects that use this intrusive approach may be connected to any number of
unrelated signals.

### Disconnection without a connection object

Support for slot disconnection by supplying an appropriate function signature,
object pointer or tracker has been introduced in version 1.2.0.

One can disconnect any number of slots using the `signal::disconnect()` method,
which proposes 4 overloads to specify the disconnection criterion:

- The first takes a reference to a callable. Any kind of callable can be passed,
  even pointers to member functions, function objects and lambdas,
- The second takes a pointer to an object, for slots bound to a pointer to member
  function, or a tracking object,
- The third overload takes both kinds of arguments at the same time and can be
  used to pinpoint a specific pair of object + callable.
- The last overload takes a group id and disconnects all the slots in this group.

Disconnection of lambdas is only possible for lambdas bound to a variable, due
to their uniqueness.

The second overload currently needs RTTI to disconnect from pointers to member
functions, function objects and lambdas. This limitation does not apply to free
and static member functions. The reasons stems from the fact that in C++, pointers
to member functions of unrelated types are not comparable, contrary to pointers to
free and static member functions. For instance, the pointer to member functions of
virtual methods of different classes can have the same address (they kind of store
the offset of the method into the vtable).

However, Sigslot can be compiled with RTTI disabled and the overload will be
deactivated for problematic cases.

As a side node, this feature admittedly added more code than anticipated at first
because it is a tricky and easy to get wrong. It has been designed carefully, with
correctness in mind, and does not have any hidden costs unless you actually use it.

Here is an example demonstrating the feature.

```cpp
#include <sigslot/signal.hpp>
#include <string>

static int i = 0;

void f1() { i += 1; }
void f2() { i += 1; }

struct s {
    void m1() { i += 1; }
    void m2() { i += 1; }
    void m3() { i += 1; }
};

struct o {
    void operator()() { i += 1; }
};

int main() {
    sigslot::signal<> sig;
    s s1;
    auto s2 = std::make_shared<s>();

    auto lbd = [&] { i += 1; };

    sig.connect(f1);           // #1
    sig.connect(f2);           // #2
    sig.connect(&s::m1, &s1);  // #3
    sig.connect(&s::m2, &s1);  // #4
    sig.connect(&s::m3, &s1);  // #5
    sig.connect(&s::m1, s2);   // #6
    sig.connect(&s::m2, s2);   // #7
    sig.connect(o{});          // #8
    sig.connect(lbd);          // #9

    sig();  // i == 9

    sig.disconnect(f2);              // #2 is removed
    sig.disconnect(&s::m1);          // #3 and #6 are removed
    sig.disconnect(o{});             // #8 and is removed
 // sig.disconnect(&o::operator());  // same as the above, more efficient
    sig.disconnect(lbd);             // #9 and is removed
    sig.disconnect(s2);              // #7 is removed
    sig.disconnect(&s::m3, &s1);     // #5 is removed, not #4

    sig();  // i == 11

    sig.disconnect_all();         // remove all remaining slots
    return 0;
}
```

### Enforcing slot invocation order with slot groups

From version 1.2.0, slots can be assigned a group id in order to control the
relative order of invocation of slots.

The order of invocation of slots in a same group is unspecified and should not be
relied upon, however slot groups are invoked in ascending group id order.
When the group id of a slot is not set, it is assigned to the group 0.
Group ids can have any value in the range of signed 32 bit integers.

```cpp
#include <sigslot/signal.hpp>
#include <cstdio>
#include <limits>

int main() {
    sigslot::signal<> sig;

    // simply assigning a group id as last argument to connect
    sig.connect([] { std::puts("Second"); }, 1);
    sig.connect([] { std::puts("Last"); }, std::numeric_limits<sigslot::group_id>::max());
    sig.connect([] { std::puts("First"); }, -10);
    sig();

    return 0;
}
```

### Signal chaining

The freestanding `sigslot::connect()` function can be used to connect a signal
to another with compatible arguments.

```cpp
#include <sigslot/signal.hpp>
#include <print>

int main() {
    sigslot::signal<int> sig1;
    sigslot::signal<double> sig2;

    sigslot::connect(sig1, sig2);
    sigslot::connect(sig2, [](double d) { std::println("got {}", d); });
    sig1(1);

    return 0;
}
```

### Thread safety

Thread safety is unit-tested. In particular, cross-signal emission and recursive
emission run fine in a multiple threads scenario.

`sigslot::signal` is a typedef to the more general `sigslot::signal_base` template
class, whose first template argument must be a Lockable type. This type will dictate
the locking policy of the class.

Sigslot offers 2 typedefs,

- `sigslot::signal` usable from multiple threads and uses std::mutex as a lockable.
  In particular, connection, disconnection, emission and slot execution are thread
  safe. It is also safe with recursive signal emission.
- `sigslot::signal_st` is a non thread-safe alternative, it trades safety for slightly
  faster operation.


## Implementation details

### Using function pointers to disconnect slots

Comparing function pointers is a nightmare in C++. Here is a table demonstrating
the size and address of a variety of cases as a showcase:

```cpp
void fun() {}

struct b1 {
    virtual ~b1() = default;
    static void sm() {}
    void m() {}
    virtual void vm() {}
};

struct b2 {
    virtual ~b2() = default;
    static void sm() {}
    void m() {}
    virtual void vm() {}
};

struct c {
    virtual ~c() = default;
    virtual void w() {}
};

struct d : b1 {
    static void sm() {}
    void m() {}
    void vm() override {}
};

struct e : b1, c {
    static void sm() {}
    void m() {}
    void vm() override{}
};
```

| Symbol  | GCC 9 Linux 64<br>Sizeof | GCC 9 Linux 64<br>Address | MSVC 16.6 32<br>Sizeof | MSVC 16.6 32<br>Address | GCC 8 Mingw 32<br>Sizeof | GCC 8 Mingw 32<br>Address | Clang-cl 9 32<br>Sizeof | Clang-cl 9 32<br>Address |
|---------|--------------------------|---------------------------|------------------------|-------------------------|--------------------------|---------------------------|-------------------------|--------------------------|
| fun     | 8                        | 0x802340                  | 4                      | 0x1311A6                | 4                        | 0xF41540                  | 4                       | 0x0010AE                 |
| &b1::sm | 8                        | 0xE03140                  | 4                      | 0x7612A5                | 4                        | 0x308D40                  | 4                       | 0x0010AE                 |
| &b1::m  | 16                       | 0xF03240                  | 4                      | 0x1514A5                | 8                        | 0x248D40                  | 4                       | 0x0010AE                 |
| &b1::vm | 16                       | 0x11                      | 4                      | 0x9F11A5                | 8                        | 0x09                      | 4                       | 0x8023AE                 |
| &b2::sm | 8                        | 0x003340                  | 4                      | 0xA515A5                | 4                        | 0x408D40                  | 4                       | 0x0010AE                 |
| &b2::m  | 16                       | 0x103440                  | 4                      | 0xEB10A5                | 8                        | 0x348D40                  | 4                       | 0x0010AE                 |
| &b2::vm | 16                       | 0x11                      | 4                      | 0x6A14A5                | 8                        | 0x09                      | 4                       | 0x8023AE                 |
| &d::sm  | 8                        | 0x203440                  | 4                      | 0x2612A5                | 4                        | 0x108D40                  | 4                       | 0x0010AE                 |
| &d::m   | 16                       | 0x303540                  | 4                      | 0x9D13A5                | 8                        | 0x048D40                  | 4                       | 0x0010AE                 |
| &d::vm  | 16                       | 0x11                      | 4                      | 0x4412A5                | 8                        | 0x09                      | 4                       | 0x8023AE                 |
| &e::sm  | 8                        | 0x403540                  | 4                      | 0xF911A5                | 4                        | 0x208D40                  | 4                       | 0x0010AE                 |
| &e::m   | 16                       | 0x503640                  | 8                      | 0x8111A5                | 8                        | 0x148D40                  | 8                       | 0x0010AE                 |
| &e::vm  | 16                       | 0x11                      | 8                      | 0xA911A5                | 8                        | 0x09                      | 8                       | 0x8023AE                 |

MSVC and Clang-cl in Release mode optimize functions with the same definition by
merging them. This is a behaviour that can be deactivated with the `/OPT:NOICF`
linker option.
Sigslot tests and examples rely on a lot a identical callables which trigger this
behaviour, which is why it deactivates this particular optimization on the affected
compilers.

### Performance Benchmarks

#### Phase 0–4 Optimization Results (January 4, 2026)

**Note:** These are microbenchmarks measuring individual operations in isolation. Real-world performance may vary depending on usage patterns, system load, and compiler optimizations.

**Test System:**
- **CPU**: AMD Ryzen 9 7950X3D 16-Core Processor @ 4.2GHz (32 logical cores)
- **OS**: Microsoft Windows 11 Pro (Build 26200)
- **Compiler**: Microsoft C/C++ Optimizing Compiler Version 19.50.35721 for x64
- **Build**: Release with LTO (/GL /LTCG), x64 target
- **Memory**: 63.16 GB total

##### Single-Threaded Benchmarks

| Benchmark | Phase 0 (ns) | Phase 4 dual-counter (ns) | Phase 5 lock-free (ns) | vs Baseline |
|-----------|--------------|---------------------------|------------------------|-------------|
| Signal Construction | 30.1 | 64.8 | 55.6 | -85% |
| Signal Destruction | 240 | 256 | 261 | -9% |
| **Connect Single Slot** | 64.6 | 167 | **129** | -100% |
| **Emission Single Slot** | **9.20** | **5.86** | **5.97** | **35% faster** |
| **Emission Multiple Slots** | **17.6** | **14.3** | **14.7** | **16% faster** |
| Slot Count | 9.04 | 6.28 | 6.18 | **32% faster** |

##### Multi-Threaded Benchmarks

| Benchmark | Phase 0 (ns) | Phase 4 dual-counter (ns) | Phase 5 lock-free (ns) | vs Baseline |
|-----------|--------------|---------------------------|------------------------|-------------|
| Thread-Safe Construction | 1.14 | 0.80 | **0.77** | **32% faster** |
| **Thread-Safe Emission** | **5.32** | **5.09** | **5.01** | **6% faster** |
| Concurrent Emission (1 thread) | 61,006 | 57,732 | 57,158 | **6% faster** |
| Concurrent Emission (2 threads) | 87,692 | 85,638 | 84,530 | **4% faster** |
| Concurrent Emission (4 threads) | 147,187 | 144,479 | 145,228 | **1% faster** |
| Concurrent Connect (1 thread) | 49,416 | 54,658 | 54,728 | -11% |
| Concurrent Connect (2 threads) | 84,034 | 84,987 | 85,945 | -2% |
| Concurrent Connect (4 threads) | ~140,000 | 138,146 | 138,749 | **1% faster** |

**Key Findings:**
- ✅ **Phase 3 SBO**: Small Buffer Optimization for up to 3 slots per group
- ✅ **Phase 4 dual-counter intrusive_ptr**: Lock-free weak references via CAS
- ✅ **Phase 5 lock-free connect/disconnect**: Mutex removed, CAS-based updates
- ✅ **Emission latency**: Improved 16-35% vs baseline (the hot path)
- ✅ **Connect improved**: 129 ns (23% faster than Phase 4 with mutex)
- ✅ **All concurrent operations**: Now faster or equal to baseline

**Current Implementation:**
- **Fully lock-free** for thread-safe signals (no mutex)
- Uses **dual-counter `intrusive_ptr`** with embedded strong + weak reference counts
- Lock-free `weak_ptr::lock()` and connect/disconnect via atomic CAS loops
- PMR (Polymorphic Memory Resource) enables custom allocation strategies
- 16 bytes for counters fits in cache line with 3-slot SBO

**Memory Layout (per slot_state):**
```
intrusive_refcount: 16 bytes (m_strong + m_weak atomics)
slot_state fields:   ~24 bytes (index, connected, blocked flags)
Total:              ~40 bytes (vs ~56 bytes with std::weak_ptr anchor)
```

**Archived Results:** `benchmark/archive/phase*_*.json`

**Running Benchmarks:**
```bash
cmake -B build -DSIGSLOT_ENABLE_BENCHMARK=ON
cmake --build build --target signal_benchmarks threaded_benchmarks
./build/benchmark/benchmarks/Release/signal_benchmarks
```

**System Information Script:**
```powershell
# Generate detailed system info for benchmark reports
.\scripts\get-system-info.ps1
```

### Known bugs

Using generic lambdas with GCC less than version 7.4 can trigger [Bug #68071](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68071).

