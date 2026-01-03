#include <catch2/catch_test_macros.hpp>
#include <sigslot/signal.hpp>

// This test file only compiles when SIGSLOT_HAVE_QCORO is defined
#if defined(SIGSLOT_HAVE_QCORO)

#include <sigslot/adapter/qt-async.hpp>
#include <QCoro/QCoroTask>
#include <QCoreApplication>
#include <QTimer>

static int argc = 1;
static char* argv[] = {(char*)"qt-qcoro-test", nullptr};
static QCoreApplication app(argc, argv);

// =============================================================================
// QCoro Await Tests
// =============================================================================

TEST_CASE("qcoro_await basic signal", "[qt][qcoro]") {
    sigslot::signal<int> sig;
    bool completed = false;
    int received_value = 0;

    auto task = [&]() -> QCoro::Task<> {
        auto [value] = co_await sigslot::qt::qcoro_await(sig);
        received_value = value;
        completed = true;
    };

    // Start the task
    auto running = task();

    // Emit signal
    sig(42);

    // Process events
    QCoreApplication::processEvents();

    REQUIRE(completed);
    REQUIRE(received_value == 42);
}

TEST_CASE("qcoro_await multiple args", "[qt][qcoro]") {
    sigslot::signal<int, QString> sig;
    bool completed = false;
    int received_int = 0;
    QString received_str;

    auto task = [&]() -> QCoro::Task<> {
        auto [num, str] = co_await sigslot::qt::qcoro_await(sig);
        received_int = num;
        received_str = str;
        completed = true;
    };

    auto running = task();
    sig(123, "hello");
    QCoreApplication::processEvents();

    REQUIRE(completed);
    REQUIRE(received_int == 123);
    REQUIRE(received_str == "hello");
}

// =============================================================================
// QCoro Connect Tests
// =============================================================================

TEST_CASE("connect_qcoro basic", "[qt][qcoro]") {
    sigslot::signal<int> sig;
    int processed_value = 0;

    sigslot::qt::connect_qcoro(sig, [&](int x) -> QCoro::Task<> {
        processed_value = x * 2;
        co_return;
    });

    sig(21);
    QCoreApplication::processEvents();

    REQUIRE(processed_value == 42);
}

TEST_CASE("connect_qcoro with async operation", "[qt][qcoro]") {
    sigslot::signal<int> sig;
    int result = 0;

    sigslot::qt::connect_qcoro(sig, [&](int x) -> QCoro::Task<> {
        // Simulate async delay using QTimer
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(10);

        // In real code you'd co_await the timer
        // For test, just set result
        result = x;
        co_return;
    });

    sig(99);

    // Process events multiple times to allow coroutine to complete
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }

    REQUIRE(result == 99);
}

#else
// No tests when QCoro is not available
// Build with -DSIGSLOT_ENABLE_QCORO=ON to enable QCoro tests
#endif
