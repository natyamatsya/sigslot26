// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: palacaze/sigslot contributors

#include <catch2/catch_test_macros.hpp>
#include <sigslot/adapter/qt.hpp>
#include <sigslot/signal.hpp>

namespace {

void f1(int i, int& sum) {
    sum += i;
}
struct o1 {
    void operator()(int i, int& sum) { sum += 2 * i; }
};

struct s {
    void f1(int i, int& sum) { sum += i; }
    void f2(int i, int& sum) const { sum += 2 * i; }
};

struct dummy {};

} // anonymous namespace

TEST_CASE("Track QSharedPointer member functions", "[qt][tracking]") {
    int sum = 0;
    sigslot::signal<int, int&> sig;

    auto s1 = QSharedPointer<s>::create();
    sig.connect(&s::f1, s1);

    auto s2 = QSharedPointer<s>::create();
    auto w2 = s2.toWeakRef();
    sig.connect(&s::f2, w2);

    sig(1, sum);
    REQUIRE(sum == 3);

    s1.clear();
    sig(1, sum);
    REQUIRE(sum == 5);

    s2.clear();
    sig(1, sum);
    REQUIRE(sum == 5);
}

TEST_CASE("Track QSharedPointer with free function and functor", "[qt][tracking]") {
    int sum = 0;
    sigslot::signal<int, int&> sig;

    auto d1 = QSharedPointer<dummy>::create();
    sig.connect(f1, d1);

    auto d2 = QSharedPointer<dummy>::create();
    auto w2 = d2.toWeakRef();
    sig.connect(o1(), w2);

    sig(1, sum);
    REQUIRE(sum == 3);

    d1.reset();
    sig(1, sum);
    REQUIRE(sum == 5);

    d2.reset();
    sig(1, sum);
    REQUIRE(sum == 5);
}

TEST_CASE("Track QWeakPointer directly", "[qt][tracking]") {
    int sum = 0;
    sigslot::signal<int, int&> sig;

    auto shared = QSharedPointer<s>::create();
    QWeakPointer<s> weak = shared.toWeakRef();

    sig.connect(&s::f1, weak);

    sig(1, sum);
    REQUIRE(sum == 1);

    shared.clear();
    sig(1, sum);
    REQUIRE(sum == 1); // Slot disconnected when weak pointer expired
}

// =============================================================================
// Migration examples: QSharedPointer instead of raw QObject*
// =============================================================================

// Example class that would typically inherit from QObject
// For thread-safe tracking, use QSharedPointer instead of raw pointers
class MyService {
public:
    explicit MyService(int& counter)
        : m_counter(counter) {}

    void handleValue(int x) { m_counter += x; }
    void handleValueConst(int x) const { m_counter += x; }

private:
    int& m_counter;
};

TEST_CASE("QSharedPointer member function - replaces QObject* pattern",
          "[qt][tracking][migration]") {
    // OLD PATTERN (no longer supported):
    //   MyQObject* obj = new MyQObject();
    //   sig.connect(&MyQObject::slot, obj);  // raw pointer tracking
    //
    // NEW PATTERN (thread-safe):
    //   auto obj = QSharedPointer<MyService>::create();
    //   sig.connect(&MyService::slot, obj);

    int counter = 0;
    sigslot::signal<int> sig;

    {
        auto service = QSharedPointer<MyService>::create(counter);
        sig.connect(&MyService::handleValue, service);

        sig(10);
        REQUIRE(counter == 10);

        // service goes out of scope here, QSharedPointer prevents premature destruction
    }
    // After QSharedPointer is destroyed, slot auto-disconnects

    sig(5);
    REQUIRE(counter == 10); // No change - slot was disconnected
}

TEST_CASE("QSharedPointer with free function - replaces QObject* tracking",
          "[qt][tracking][migration]") {
    // This pattern: track object lifetime but use a free function as slot
    // OLD: sig.connect(freeFunc, qobject_ptr);
    // NEW: sig.connect(freeFunc, qsharedptr);

    int counter = 0;
    sigslot::signal<int, int&> sig;

    auto tracker = QSharedPointer<dummy>::create();
    sig.connect(f1, tracker);

    sig(7, counter);
    REQUIRE(counter == 7);

    tracker.clear(); // Explicitly release
    sig(3, counter);
    REQUIRE(counter == 7); // Slot disconnected
}

TEST_CASE("Scoped QSharedPointer lifetime", "[qt][tracking][migration]") {
    // Demonstrates RAII-style automatic disconnection
    int counter = 0;
    sigslot::signal<int> sig;

    {
        auto service = QSharedPointer<MyService>::create(counter);
        sig.connect(&MyService::handleValueConst, service);

        sig(1);
        sig(2);
        sig(3);
        REQUIRE(counter == 6);
    }
    // service destroyed here - slot automatically disconnected

    sig(100);
    REQUIRE(counter == 6); // Still 6 - slot was disconnected
}

TEST_CASE("Multiple QSharedPointer references keep slot alive", "[qt][tracking][migration]") {
    int counter = 0;
    sigslot::signal<int> sig;

    auto service1 = QSharedPointer<MyService>::create(counter);
    auto service2 = service1; // Second reference

    sig.connect(&MyService::handleValue, service1);

    sig(5);
    REQUIRE(counter == 5);

    service1.clear(); // First reference gone
    sig(5);
    REQUIRE(counter == 10); // Still works - service2 keeps it alive

    service2.clear(); // Last reference gone
    sig(5);
    REQUIRE(counter == 10); // Now disconnected
}

TEST_CASE("Bridge legacy raw pointer with non-owning QSharedPointer",
          "[qt][tracking][migration][bridge]") {
    // This pattern allows tracking objects you don't own (e.g., Qt parent-owned)
    // Use a null deleter so QSharedPointer won't delete the object

    int counter = 0;
    sigslot::signal<int> sig;

    // Simulate a legacy object owned elsewhere (stack, Qt parent, etc.)
    MyService legacyService(counter);

    {
        // Create non-owning QSharedPointer with null deleter
        auto tracked = QSharedPointer<MyService>(&legacyService, [](MyService*) {});

        sig.connect(&MyService::handleValue, tracked);

        sig(10);
        REQUIRE(counter == 10);

        // tracked goes out of scope - releases reference but doesn't delete
    }
    // Slot is now disconnected because tracked QSharedPointer was destroyed

    sig(5);
    REQUIRE(counter == 10); // No change - slot disconnected

    // legacyService is still valid (stack-allocated), just not connected
}

TEST_CASE("Bridge with QWeakPointer for deferred connection check",
          "[qt][tracking][migration][bridge]") {
    // Advanced pattern: keep QWeakPointer to check validity before use

    int counter = 0;
    sigslot::signal<int> sig;

    MyService legacyService(counter);

    // Create non-owning shared pointer
    auto tracked = QSharedPointer<MyService>(&legacyService, [](MyService*) {});
    QWeakPointer<MyService> weak = tracked.toWeakRef();

    sig.connect(&MyService::handleValue, weak);

    sig(7);
    REQUIRE(counter == 7);

    // Check if still valid
    REQUIRE_FALSE(weak.isNull());

    tracked.clear(); // Release the tracking pointer

    // Now weak is expired
    REQUIRE(weak.isNull());

    sig(3);
    REQUIRE(counter == 7); // Slot disconnected
}
