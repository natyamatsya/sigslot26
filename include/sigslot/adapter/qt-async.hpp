// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: mousebyte/sigslot20 contributors
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

/**
 * @file qt-async.hpp
 * @brief Qt async integration for sigslot signals
 * 
 * Provides integration between sigslot signals and Qt's async mechanisms:
 * - connect_on_event_loop() - Execute slots on Qt's event loop
 * - as_qfuture() - Convert signal emission to QFuture<T>
 * - signal_awaiter for QFuture - Coroutine support for awaiting signals
 * 
 * Optional QCoro support when SIGSLOT_HAVE_QCORO is defined.
 * 
 * Requires Qt 6.5+
 */

#include "../signal.hpp"
#include "qt.hpp"
#include <QObject>
#include <QFuture>
#include <QPromise>
#include <QTimer>
#include <QCoreApplication>
#include <QThread>
#include <functional>
#include <memory>
#include <tuple>

// Coroutine support - must be included before namespace to avoid lookup issues
#if __has_include(<coroutine>)
#include <compare> // Required before <coroutine> on Apple Clang
#include <coroutine>
#define SIGSLOT_QT_HAS_COROUTINES 1
#endif

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
#error "Qt 6.5 or later is required for qt-async.hpp"
#endif

namespace sigslot::qt {

// =============================================================================
// Event Loop Integration
// =============================================================================

namespace detail {

/**
 * @brief Base class for type-erased invocation events
 * 
 * Uses an atomic ready flag with acquire/release semantics to ensure
 * memory visibility of derived class members across threads.
 * 
 * Memory Model:
 * - Producer calls mark_ready() after fully constructing the event
 * - Consumer calls wait_ready() before accessing any event data
 * - The release-store in mark_ready() synchronizes-with the acquire-load
 *   in wait_ready(), establishing a happens-before relationship
 * 
 * The while loop in wait_ready() is technically unnecessary since Qt's
 * postEvent guarantees the event is fully posted before it can be processed.
 * However, the explicit acquire barrier is required to make the memory
 * effects visible to the receiving thread, and the loop makes this
 * synchronization pattern clear to ThreadSanitizer.
 * 
 * IMPORTANT: wait_ready() must be called BEFORE the virtual invoke() call,
 * not inside it, because the vptr itself must be visible before virtual
 * dispatch occurs.
 */
class invoke_event_base : public QEvent {
public:
    static const QEvent::Type EventType;

    invoke_event_base()
        : QEvent(EventType) {}
    ~invoke_event_base() override = default;
    virtual void invoke() = 0;

    void mark_ready() { ready_.store(true, std::memory_order_release); }
    void wait_ready() const {
        while (!ready_.load(std::memory_order_acquire)) {}
    }

private:
    std::atomic<bool> ready_{false};
};

inline const QEvent::Type invoke_event_base::EventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());

/**
 * @brief Templated invocation event - stores slot and args directly
 * 
 * Memory visibility is ensured via the base class ready flag.
 */
template<typename Slot, typename... Args>
class invoke_event final : public invoke_event_base {
public:
    invoke_event(QSharedPointer<Slot> slot, Args... args)
        : slot_(std::move(slot))
        , args_(std::move(args)...) {}

    void invoke() override { std::apply(*slot_, args_); }

private:
    QSharedPointer<Slot> slot_;
    std::tuple<std::decay_t<Args>...> args_;
};

/**
 * @brief Helper QObject to receive and execute invocation events
 */
class event_receiver : public QObject {
public:
    explicit event_receiver(QObject* parent = nullptr)
        : QObject(parent) {}

protected:
    bool event(QEvent* e) override {
        if (e->type() == invoke_event_base::EventType) {
            auto* inv = static_cast<invoke_event_base*>(e);
            inv->wait_ready(); // Acquire barrier BEFORE virtual call (ensures vptr visible)
            inv->invoke();
            return true;
        }
        return QObject::event(e);
    }
};

/**
 * @brief Post a slot invocation to execute on a QObject's thread
 * 
 * Thread-safe: uses atomic ready flag for memory visibility.
 */
template<typename Slot, typename... Args>
void post_invoke(QObject* receiver, QSharedPointer<Slot> slot, Args&&... args) {
    auto* event =
        new invoke_event<Slot, std::decay_t<Args>...>(std::move(slot), std::forward<Args>(args)...);
    event->mark_ready(); // Release barrier - makes slot and args visible
    QCoreApplication::postEvent(receiver, event);
}

} // namespace detail

/**
 * @brief Connect a slot that executes on Qt's event loop
 * 
 * The slot will be queued for execution on the Qt event loop,
 * making it safe to update Qt UI elements from signal handlers.
 * 
 * @tparam Args Signal argument types
 * @tparam Slot The slot callable type
 * @param sig The signal to connect to
 * @param slot The slot to execute
 * @return connection object for managing the connection
 * 
 * @code
 * sigslot::signal<int> sig;
 * sigslot::qt::connect_on_event_loop(sig, [](int x) {
 *     // Safe to update UI here
 *     myLabel->setText(QString::number(x));
 * });
 * @endcode
 */
template<typename... Args, typename Slot>
connection connect_on_event_loop(signal<Args...>& sig, Slot&& slot) {
    auto receiver = QSharedPointer<detail::event_receiver>::create();
    auto slot_ptr = QSharedPointer<std::decay_t<Slot>>::create(std::forward<Slot>(slot));

    auto wrapper = [receiver, slot_ptr](Args... args) {
        detail::post_invoke(receiver.data(), slot_ptr, args...);
    };

    return sig.connect(std::move(wrapper));
}

/**
 * @brief Connect a slot that executes on Qt's event loop with lifetime tracking
 * 
 * The slot will be queued for execution on the Qt event loop and automatically
 * disconnected when the tracked object is destroyed.
 * 
 * @tparam Args Signal argument types
 * @tparam Slot The slot callable type
 * @tparam T The tracked object type
 * @param sig The signal to connect to
 * @param slot The slot to execute
 * @param tracked QSharedPointer for automatic disconnection when destroyed
 * @return connection object for managing the connection
 * 
 * @code
 * sigslot::signal<int> sig;
 * auto controller = QSharedPointer<MyController>::create();
 * sigslot::qt::connect_on_event_loop(sig, [](int x) {
 *     // Safe to update UI here
 * }, controller);
 * // Slot auto-disconnects when controller is destroyed
 * @endcode
 */
template<typename... Args, typename Slot, typename T>
connection connect_on_event_loop(signal<Args...>& sig, Slot&& slot, QSharedPointer<T> tracked) {
    auto receiver = QSharedPointer<detail::event_receiver>::create();
    auto slot_ptr = QSharedPointer<std::decay_t<Slot>>::create(std::forward<Slot>(slot));

    auto wrapper = [receiver, slot_ptr](Args... args) {
        detail::post_invoke(receiver.data(), slot_ptr, args...);
    };

    return sig.connect(std::move(wrapper), std::move(tracked));
}

/**
 * @brief Connect a slot to execute on a specific Qt thread
 * 
 * @tparam Args Signal argument types
 * @tparam Slot The slot callable type
 * @param sig The signal to connect to
 * @param thread The target thread for slot execution
 * @param slot The slot to execute
 * @return connection object
 */
template<typename... Args, typename Slot>
connection connect_on_thread(signal<Args...>& sig, QThread* thread, Slot&& slot) {
    auto receiver = QSharedPointer<detail::event_receiver>::create();
    receiver->moveToThread(thread);
    auto slot_ptr = QSharedPointer<std::decay_t<Slot>>::create(std::forward<Slot>(slot));

    auto wrapper = [receiver, slot_ptr](Args... args) {
        detail::post_invoke(receiver.data(), slot_ptr, args...);
    };

    return sig.connect(std::move(wrapper));
}

/**
 * @brief Connect a slot to execute on a specific Qt thread with lifetime tracking
 * 
 * The slot will be executed on the specified thread and automatically
 * disconnected when the tracked object is destroyed.
 * 
 * @tparam Args Signal argument types
 * @tparam Slot The slot callable type
 * @tparam T The tracked object type
 * @param sig The signal to connect to
 * @param thread The target thread for slot execution
 * @param slot The slot to execute
 * @param tracked QSharedPointer for automatic disconnection when destroyed
 * @return connection object
 * 
 * @code
 * QThread workerThread;
 * auto controller = QSharedPointer<MyController>::create();
 * sigslot::qt::connect_on_thread(sig, &workerThread, [](int x) {
 *     // Executes on workerThread
 * }, controller);
 * // Slot auto-disconnects when controller is destroyed
 * @endcode
 */
template<typename... Args, typename Slot, typename T>
connection connect_on_thread(signal<Args...>& sig, QThread* thread, Slot&& slot,
                             QSharedPointer<T> tracked) {
    auto receiver = QSharedPointer<detail::event_receiver>::create();
    receiver->moveToThread(thread);
    auto slot_ptr = QSharedPointer<std::decay_t<Slot>>::create(std::forward<Slot>(slot));

    auto wrapper = [receiver, slot_ptr](Args... args) {
        detail::post_invoke(receiver.data(), slot_ptr, args...);
    };

    return sig.connect(std::move(wrapper), std::move(tracked));
}

// =============================================================================
// QFuture Integration
// =============================================================================

/**
 * @brief Convert next signal emission to a QFuture
 * 
 * Creates a QFuture that will be fulfilled with the signal's arguments
 * when the signal is next emitted.
 * 
 * @tparam Args Signal argument types
 * @param sig The signal to wait for
 * @return QFuture that completes on next emission
 * 
 @code
 * @code
 * sigslot::signal<int, QString> sig;
 * QFuture<std::tuple<int, QString>> future = sigslot::qt::as_qfuture(sig);
 * 
 * // Later, when sig emits:
 * sig(42, "hello");
 * // future is now ready with {42, "hello"}
 * @endcode
 */
template<typename... Args>
QFuture<std::tuple<Args...>> as_qfuture(signal<Args...>& sig) {
    auto promise = std::make_shared<QPromise<std::tuple<Args...>>>();
    promise->start();

    auto conn = std::make_shared<connection>();
    *conn = sig.connect([promise, conn](Args... args) mutable {
        promise->addResult(std::make_tuple(args...));
        promise->finish();
        conn->disconnect();
    });

    return promise->future();
}

/**
 * @brief Specialization for single-argument signals
 */
template<typename T>
QFuture<T> as_qfuture_single(signal<T>& sig) {
    auto promise = std::make_shared<QPromise<T>>();
    promise->start();

    auto conn = std::make_shared<connection>();
    *conn = sig.connect([promise, conn](T value) mutable {
        promise->addResult(std::move(value));
        promise->finish();
        conn->disconnect();
    });

    return promise->future();
}

/**
 * @brief Specialization for void signals
 */
inline QFuture<void> as_qfuture(signal<>& sig) {
    auto promise = std::make_shared<QPromise<void>>();
    promise->start();

    auto conn = std::make_shared<connection>();
    *conn = sig.connect([promise, conn]() mutable {
        promise->finish();
        conn->disconnect();
    });

    return promise->future();
}

/**
 * @brief Convert signal emission to QFuture with timeout
 * 
 * @tparam Args Signal argument types
 * @param sig The signal to wait for
 * @param timeout_ms Timeout in milliseconds
 * @return QFuture that completes on emission or is cancelled on timeout
 */
template<typename... Args>
QFuture<std::tuple<Args...>> as_qfuture_timeout(signal<Args...>& sig, int timeout_ms) {
    auto promise = std::make_shared<QPromise<std::tuple<Args...>>>();
    promise->start();

    auto conn = std::make_shared<connection>();
    auto timer = std::make_shared<QTimer>();
    timer->setSingleShot(true);

    *conn = sig.connect([promise, conn, timer](Args... args) mutable {
        timer->stop();
        promise->addResult(std::make_tuple(args...));
        promise->finish();
        conn->disconnect();
    });

    QObject::connect(timer.get(), &QTimer::timeout, [promise, conn]() {
        conn->disconnect();
        promise->future().cancel();
        promise->finish();
    });

    timer->start(timeout_ms);

    return promise->future();
}

// =============================================================================
// Coroutine Support for QFuture
// =============================================================================

#ifdef SIGSLOT_QT_HAS_COROUTINES

/**
 * @brief Awaiter for QFuture in C++20 coroutines
 * 
 * Allows awaiting a QFuture in a coroutine without blocking.
 * Uses Qt's event loop for suspension.
 */
template<typename T>
class qfuture_awaiter {
public:
    explicit qfuture_awaiter(QFuture<T> future)
        : future_(std::move(future)) {}

    bool await_ready() const noexcept { return future_.isFinished(); }

    void await_suspend(std::coroutine_handle<> handle) {
        // Use QFutureWatcher to resume when ready
        watcher_ = std::make_unique<QFutureWatcher<T>>();
        QObject::connect(watcher_.get(), &QFutureWatcher<T>::finished,
                         [handle]() mutable { handle.resume(); });
        watcher_->setFuture(future_);
    }

    T await_resume() {
        if constexpr (std::is_void_v<T>) {
            future_.waitForFinished();
        } else {
            return future_.result();
        }
    }

private:
    QFuture<T> future_;
    std::unique_ptr<QFutureWatcher<T>> watcher_;
};

/**
 * @brief Make QFuture awaitable
 */
template<typename T>
qfuture_awaiter<T> operator co_await(QFuture<T> future) {
    return qfuture_awaiter<T>(std::move(future));
}

/**
 * @brief Await next signal emission in a coroutine
 * 
 @code
 * @code
 * sigslot::qt::task<void> my_coro(sigslot::signal<int>& sig) {
 *     auto [value] = co_await sigslot::qt::await_signal(sig);
 *     qDebug() << "Received:" << value;
 * }
 * @endcode
 */
template<typename... Args>
QFuture<std::tuple<Args...>> await_signal(signal<Args...>& sig) {
    return as_qfuture(sig);
}

#endif // SIGSLOT_QT_HAS_COROUTINES

// =============================================================================
// Optional QCoro Support
// =============================================================================

#if defined(SIGSLOT_HAVE_QCORO)
#include <QCoro/QCoroTask>
#include <QCoro/QCoroFuture>

/**
 * @brief Create a QCoro::Task that waits for signal emission
 * 
 * Requires QCoro library. Enable with -DSIGSLOT_ENABLE_QCORO=ON
 * 
 @code
 * @code
 * QCoro::Task<> handleSignal(sigslot::signal<int>& sig) {
 *     auto [value] = co_await sigslot::qt::qcoro_await(sig);
 *     qDebug() << "Got value:" << value;
 * }
 * @endcode
 */
template<typename... Args>
QCoro::Task<std::tuple<Args...>> qcoro_await(signal<Args...>& sig) {
    co_return co_await as_qfuture(sig);
}

/**
 * @brief Connect a QCoro coroutine as a slot
 * 
 @code
 * @code
 * sigslot::signal<int> sig;
 * sigslot::qt::connect_qcoro(sig, [](int x) -> QCoro::Task<> {
 *     co_await someAsyncOperation(x);
 *     qDebug() << "Done processing" << x;
 * });
 * @endcode
 */
template<typename... Args, typename CoroSlot>
connection connect_qcoro(signal<Args...>& sig, CoroSlot&& slot) {
    return sig.connect([slot = std::forward<CoroSlot>(slot)](Args... args) mutable {
        // Fire and forget the coroutine
        [](CoroSlot s, Args... a) -> QCoro::Task<> { co_await s(a...); }(slot, args...);
    });
}

#endif // SIGSLOT_HAVE_QCORO

} // namespace sigslot::qt

// Include MOC-generated code when using Qt's meta-object system
// Users should include this header in exactly one .cpp file with:
// #include <sigslot/adapter/qt-async.hpp>
// and ensure the file is processed by moc (AUTOMOC in CMake)
