#include <catch2/catch_test_macros.hpp>
#include <sigslot/adapter/qt-async.hpp>
#include <sigslot/signal.hpp>
#include <atomic>
#include <QCoreApplication>
#include <QTimer>
#include <QThread>
#include <QFutureWatcher>

// Qt requires QCoreApplication for event loop
// Create a static instance for tests
static int argc = 1;
static char* argv[] = {(char*)"qt-async-test", nullptr};
static QCoreApplication app(argc, argv);

// =============================================================================
// Event Loop Integration Tests
// =============================================================================

TEST_CASE("connect_on_event_loop basic", "[qt][async]") {
    int sum = 0;
    sigslot::signal<int> sig;

    sigslot::qt::connect_on_event_loop(sig, [&](int x) { sum += x; });

    sig(10);
    QCoreApplication::processEvents();

    REQUIRE(sum == 10);
}

TEST_CASE("connect_on_event_loop multiple slots", "[qt][async]") {
    int sum = 0;
    sigslot::signal<int> sig;

    sigslot::qt::connect_on_event_loop(sig, [&](int x) { sum += x; });
    sigslot::qt::connect_on_event_loop(sig, [&](int x) { sum += x * 2; });

    sig(5);
    QCoreApplication::processEvents();

    REQUIRE(sum == 15); // 5 + 10
}

TEST_CASE("connect_on_event_loop with QSharedPointer tracking", "[qt][async][tracking]") {
    int sum = 0;
    sigslot::signal<int> sig;

    struct Tracker {};

    {
        auto tracked = QSharedPointer<Tracker>::create();
        sigslot::qt::connect_on_event_loop(sig, [&](int x) { sum += x; }, tracked);

        sig(10);
        QCoreApplication::processEvents();
        REQUIRE(sum == 10);

        // tracked goes out of scope - slot should disconnect
    }

    sig(5);
    QCoreApplication::processEvents();
    REQUIRE(sum == 10); // No change - slot was disconnected
}

TEST_CASE("connect_on_event_loop QSharedPointer multiple references", "[qt][async][tracking]") {
    int sum = 0;
    sigslot::signal<int> sig;

    struct Tracker {};

    auto tracked1 = QSharedPointer<Tracker>::create();
    auto tracked2 = tracked1; // Second reference

    sigslot::qt::connect_on_event_loop(sig, [&](int x) { sum += x; }, tracked1);

    sig(5);
    QCoreApplication::processEvents();
    REQUIRE(sum == 5);

    tracked1.clear(); // First reference gone
    sig(5);
    QCoreApplication::processEvents();
    REQUIRE(sum == 10); // Still works - tracked2 keeps it alive

    tracked2.clear(); // Last reference gone
    sig(5);
    QCoreApplication::processEvents();
    REQUIRE(sum == 10); // No change - slot disconnected
}

// =============================================================================
// QFuture Integration Tests
// =============================================================================

TEST_CASE("as_qfuture with multiple args", "[qt][async][qfuture]") {
    sigslot::signal<int, QString> sig;

    auto future = sigslot::qt::as_qfuture(sig);
    REQUIRE_FALSE(future.isFinished());

    sig(42, "hello");

    REQUIRE(future.isFinished());
    auto [num, str] = future.result();
    REQUIRE(num == 42);
    REQUIRE(str == "hello");
}

TEST_CASE("as_qfuture_single", "[qt][async][qfuture]") {
    sigslot::signal<int> sig;

    auto future = sigslot::qt::as_qfuture_single(sig);
    REQUIRE_FALSE(future.isFinished());

    sig(123);

    REQUIRE(future.isFinished());
    REQUIRE(future.result() == 123);
}

TEST_CASE("as_qfuture void signal", "[qt][async][qfuture]") {
    sigslot::signal<> sig;

    auto future = sigslot::qt::as_qfuture(sig);
    REQUIRE_FALSE(future.isFinished());

    sig();

    REQUIRE(future.isFinished());
}

TEST_CASE("as_qfuture with QFutureWatcher", "[qt][async][qfuture]") {
    int result = 0;
    sigslot::signal<int> sig;

    auto future = sigslot::qt::as_qfuture_single(sig);

    QFutureWatcher<int> watcher;
    QObject::connect(&watcher, &QFutureWatcher<int>::finished,
                     [&]() { result = watcher.result(); });
    watcher.setFuture(future);

    sig(999);
    QCoreApplication::processEvents();

    REQUIRE(result == 999);
}

// =============================================================================
// Thread Tests
// =============================================================================

TEST_CASE("connect_on_thread", "[qt][async][thread]") {
    std::atomic<int> sum{0};
    sigslot::signal<int> sig;

    QThread workerThread;
    workerThread.start();

    sigslot::qt::connect_on_thread(sig, &workerThread, [&](int x) {
        sum.store(sum.load() + x, std::memory_order_release);
    });

    sig(50);

    // Give thread time to process
    QThread::msleep(50);
    QCoreApplication::processEvents();

    workerThread.quit();
    workerThread.wait();

    REQUIRE(sum.load(std::memory_order_acquire) == 50);
}

// =============================================================================
// Timeout Tests
// =============================================================================

TEST_CASE("as_qfuture_timeout success", "[qt][async][timeout]") {
    sigslot::signal<int> sig;

    auto future = sigslot::qt::as_qfuture_timeout(sig, 1000);

    // Emit before timeout
    QTimer::singleShot(10, [&]() { sig(42); });

    // Process events
    for (int i = 0; i < 10 && !future.isFinished(); ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    REQUIRE(future.isFinished());
    REQUIRE_FALSE(future.isCanceled());
    auto [value] = future.result();
    REQUIRE(value == 42);
}
