#pragma once

/**
 * @file execution.hpp
 * @brief std::execution (P2300) support for sigslot signals
 * 
 * This header provides sender/receiver integration for signals using
 * NVIDIA's stdexec library (reference implementation of P2300).
 * 
 * Feature macro: SIGSLOT_HAVE_STDEXEC
 * 
 * When enabled, signals can be used as senders in the sender/receiver
 * model, allowing integration with schedulers and async algorithms.
 * 
 * All execution support is in the sigslot::async namespace, unified
 * with coroutine support.
 */

#include "../signal.hpp"

// Check for stdexec availability
#if defined(SIGSLOT_HAVE_STDEXEC)

#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>

namespace sigslot::async {

/**
 * @brief Signal sender - adapts a signal to the sender/receiver model
 * 
 * This sender completes with the signal's argument types when the
 * signal is emitted. It connects a one-shot slot that forwards
 * the emission to the receiver.
 */
template<typename... Args>
class signal_sender {
public:
    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(Args...), stdexec::set_stopped_t()>;

private:
    signal<Args...>* sig_;

public:
    explicit signal_sender(signal<Args...>& sig) noexcept
        : sig_(&sig) {}

    template<typename Receiver>
    class operation_state {
        // Shared state to safely communicate between threads
        struct shared_state {
            std::mutex mtx;
            Receiver rcv;
            connection conn;
            bool completed = false;

            explicit shared_state(Receiver r)
                : rcv(std::move(r)) {}
        };

        signal<Args...>* sig_;
        std::shared_ptr<shared_state> state_;

    public:
        operation_state(signal<Args...>* sig, Receiver rcv)
            : sig_(sig)
            , state_(std::make_shared<shared_state>(std::move(rcv))) {}

        operation_state(operation_state&&) = delete;
        operation_state& operator=(operation_state&&) = delete;

        void start() noexcept {
            auto state = state_;
            try {
                std::lock_guard<std::mutex> lock(state->mtx);
                state->conn = sig_->connect([state](Args... args) {
                    std::lock_guard<std::mutex> inner_lock(state->mtx);
                    if (!state->completed) {
                        state->completed = true;
                        state->conn.disconnect();
                        stdexec::set_value(std::move(state->rcv), std::forward<Args>(args)...);
                    }
                });
            } catch (...) {
                std::lock_guard<std::mutex> err_lock(state->mtx);
                if (!state->completed) {
                    state->completed = true;
                    stdexec::set_stopped(std::move(state->rcv));
                }
            }
        }
    };

    template<stdexec::receiver Receiver>
    auto connect(Receiver rcv) const noexcept {
        return operation_state<Receiver>(sig_, std::move(rcv));
    }

    auto get_env() const noexcept { return stdexec::env<>{}; }
};

/**
 * @brief Signal sender for single-threaded signals
 @note Single-threaded signals should only be used from one thread,
 * but we still use shared_ptr for safe receiver management.
 */
template<typename... Args>
class signal_st_sender {
public:
    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(Args...), stdexec::set_stopped_t()>;

private:
    signal_st<Args...>* sig_;

public:
    explicit signal_st_sender(signal_st<Args...>& sig) noexcept
        : sig_(&sig) {}

    template<typename Receiver>
    class operation_state {
        struct shared_state {
            Receiver rcv;
            connection conn;
            bool completed = false;

            explicit shared_state(Receiver r)
                : rcv(std::move(r)) {}
        };

        signal_st<Args...>* sig_;
        std::shared_ptr<shared_state> state_;

    public:
        operation_state(signal_st<Args...>* sig, Receiver rcv)
            : sig_(sig)
            , state_(std::make_shared<shared_state>(std::move(rcv))) {}

        operation_state(operation_state&&) = delete;
        operation_state& operator=(operation_state&&) = delete;

        void start() noexcept {
            auto state = state_;
            try {
                state->conn = sig_->connect([state](Args... args) {
                    if (!state->completed) {
                        state->completed = true;
                        state->conn.disconnect();
                        stdexec::set_value(std::move(state->rcv), std::forward<Args>(args)...);
                    }
                });
            } catch (...) {
                if (!state->completed) {
                    state->completed = true;
                    stdexec::set_stopped(std::move(state->rcv));
                }
            }
        }
    };

    template<stdexec::receiver Receiver>
    auto connect(Receiver rcv) const noexcept {
        return operation_state<Receiver>(sig_, std::move(rcv));
    }

    auto get_env() const noexcept { return stdexec::env<>{}; }
};

/**
 * @brief Infinite signal sender - yields values each time signal emits
 * 
 * Unlike signal_sender which completes after one emission,
 * this sender can be used with algorithms that consume multiple values.
 */
template<typename... Args>
class signal_stream_sender {
public:
    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(Args...), stdexec::set_stopped_t()>;

private:
    signal<Args...>* sig_;

public:
    explicit signal_stream_sender(signal<Args...>& sig) noexcept
        : sig_(&sig) {}

    signal<Args...>* signal_ptr() const noexcept { return sig_; }

    auto get_env() const noexcept { return stdexec::env<>{}; }
};

// Factory functions

/**
 * @brief Create a sender from a signal (one-shot)
 * 
 * The sender completes with the signal's arguments when the
 * signal is next emitted.
 * 
 * @param sig The signal to adapt
 * @return A sender that completes on next signal emission
 */
template<typename... Args>
auto as_sender(signal<Args...>& sig) {
    return signal_sender<Args...>(sig);
}

template<typename... Args>
auto as_sender(signal_st<Args...>& sig) {
    return signal_st_sender<Args...>(sig);
}

/**
 * @brief Create a streaming sender from a signal
 * 
 * Use with algorithms like repeat() to handle multiple emissions.
 * 
 * @param sig The signal to adapt
 * @return A sender for streaming signal emissions
 */
template<typename... Args>
auto as_stream(signal<Args...>& sig) {
    return signal_stream_sender<Args...>(sig);
}

/**
 * @brief Execute a slot on a specific scheduler
 * 
 * Creates a connection where the slot executes on the given scheduler
 * instead of the emitting thread.
 * 
 * @param sig The signal to connect to
 * @param sched The scheduler to execute on
 * @param slot The slot to execute
 * @return A scoped_connection managing the subscription
 */
template<typename... Args, stdexec::scheduler Scheduler, typename Slot>
auto connect_on(signal<Args...>& sig, Scheduler scheduler, Slot&& slot_fn) {
    return sig.connect([sched = std::move(scheduler),
                        slot = std::forward<Slot>(slot_fn)](Args... args) mutable {
        // Capture args and execute slot on scheduler
        auto work = stdexec::then(stdexec::schedule(sched),
                                  [&slot, ... captured_args = std::forward<Args>(args)]() mutable {
                                      slot(std::forward<Args>(captured_args)...);
                                  });
        stdexec::sync_wait(std::move(work));
    });
}

/**
 * @brief Execute a slot on a specific scheduler (async, fire-and-forget)
 * 
 * Like connect_on but doesn't block the emitting thread.
 * Requires an async_scope to manage the operation lifetime.
 * 
 * @param sig The signal to connect to
 * @param scope The async_scope to spawn work into
 * @param sched The scheduler to execute on
 * @param slot The slot to execute
 * @return A scoped_connection managing the subscription
 */
template<typename... Args, stdexec::scheduler Scheduler, typename Slot>
auto connect_on_async(signal<Args...>& sig, ::exec::async_scope& scope, Scheduler scheduler,
                      Slot&& slot_fn) {
    return sig.connect([&scope, sched = std::move(scheduler),
                        slot = std::forward<Slot>(slot_fn)](Args... args) mutable {
        auto work = stdexec::then(stdexec::schedule(sched),
                                  [&slot, ... captured_args = std::forward<Args>(args)]() mutable {
                                      slot(std::forward<Args>(captured_args)...);
                                  });
        scope.spawn(std::move(work));
    });
}

// =============================================================================
// Coroutine / Execution Interop
// =============================================================================

/**
 * @brief Awaitable sender wrapper for use in coroutines
 * 
 * Allows using senders with co_await in coroutines.
 * stdexec provides this via as_awaitable, but we provide a convenience wrapper.
 */
template<typename Sender>
class sender_awaitable {
    Sender sender_;

public:
    explicit sender_awaitable(Sender s)
        : sender_(std::move(s)) {}

    bool await_ready() const noexcept { return false; }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> h) {
        return stdexec::as_awaitable(std::move(sender_), h.promise()).await_suspend(h);
    }

    auto await_resume() {
        // This will be handled by stdexec's awaitable
        return stdexec::sync_wait(std::move(sender_));
    }
};

/**
 * @brief Create an awaitable from a signal sender for use in coroutines
 * 
 * @param sig The signal to await
 * @return An awaitable that can be used with co_await
 */
template<typename... Args>
auto as_awaitable(signal<Args...>& sig) {
    return sender_awaitable<signal_sender<Args...>>(signal_sender<Args...>(sig));
}

/**
 * @brief Connect a coroutine-returning slot to a signal, executing on a scheduler
 * 
 * The coroutine slot is spawned on the given scheduler when the signal emits.
 * This is useful for slots that need to perform async work.
 * 
 * @param sig The signal to connect to
 * @param scope The async_scope to manage coroutine lifetime
 * @param sched The scheduler to run the coroutine on
 * @param coro_slot A callable that returns a task<void> coroutine
 * @return A connection managing the subscription
 * 
 @code
 *   auto conn = connect_coro_on(sig, scope, sched, [](int x) -> async::task<void> {
 *       co_await some_async_work(x);
 *   });
 */
template<typename... Args, stdexec::scheduler Scheduler, typename CoroSlot>
auto connect_coro_on(signal<Args...>& sig, ::exec::async_scope& scope, Scheduler scheduler,
                     CoroSlot&& coro_slot_fn) {
    return sig.connect([&scope, sched = std::move(scheduler),
                        coro_slot = std::forward<CoroSlot>(coro_slot_fn)](Args... args) mutable {
        // Create work that schedules the coroutine execution
        auto work =
            stdexec::then(stdexec::schedule(sched),
                          [&coro_slot, ... captured_args = std::forward<Args>(args)]() mutable {
                              // Invoke the coroutine slot - it manages its own lifetime
                              auto t = coro_slot(std::forward<Args>(captured_args)...);
                              // task<void> will execute eagerly and clean up
                          });
        scope.spawn(std::move(work));
    });
}

/**
 * @brief Synchronously wait for a signal emission in a blocking manner
 * 
 * This is a convenience function that combines as_sender with sync_wait.
 * Useful for simple cases where you want to block until a signal emits.
 * 
 * @param sig The signal to wait for
 * @return A tuple of the signal's arguments
 */
template<typename... Args>
auto sync_wait_for(signal<Args...>& sig) {
    return stdexec::sync_wait(as_sender(sig));
}

} // namespace sigslot::async

// Extend signal_base with sender support
namespace sigslot::detail {

template<typename... Args, typename Lockable, typename Group>
auto make_sender(signal_base<Lockable, Group, Args...>& sig) {
    if constexpr (std::is_same_v<Lockable, detail::null_mutex>) {
        return async::signal_st_sender<Args...>(static_cast<signal_st<Args...>&>(sig));
    } else {
        return async::signal_sender<Args...>(static_cast<signal<Args...>&>(sig));
    }
}

} // namespace sigslot::detail

#else // !SIGSLOT_HAVE_STDEXEC

namespace sigslot::async {

// Stub implementations when stdexec is not available
// These provide compile-time errors with helpful messages

template<typename... Args>
struct signal_sender {
    static_assert(sizeof...(Args) == 0 && sizeof...(Args) != 0,
                  "std::execution support requires SIGSLOT_ENABLE_STDEXEC=ON in CMake");
};

template<typename Signal>
auto as_sender(Signal&) {
    static_assert(sizeof(Signal) == 0,
                  "std::execution support requires SIGSLOT_ENABLE_STDEXEC=ON in CMake");
}

template<typename Signal>
auto as_stream(Signal&) {
    static_assert(sizeof(Signal) == 0,
                  "std::execution support requires SIGSLOT_ENABLE_STDEXEC=ON in CMake");
}

template<typename Signal, typename Scheduler, typename Slot>
auto connect_on(Signal&, Scheduler, Slot&&) {
    static_assert(sizeof(Signal) == 0,
                  "std::execution support requires SIGSLOT_ENABLE_STDEXEC=ON in CMake");
}

} // namespace sigslot::async

#endif // SIGSLOT_HAVE_STDEXEC

// Feature detection macro for user code
#if defined(SIGSLOT_HAVE_STDEXEC)
#define SIGSLOT_EXECUTION_AVAILABLE 1
#else
#define SIGSLOT_EXECUTION_AVAILABLE 0
#endif
