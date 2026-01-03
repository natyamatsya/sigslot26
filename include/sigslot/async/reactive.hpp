// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

/**
 * @file reactive.hpp
 * @brief Reactive extensions for sigslot signals
 *
 * Provides functional operators for transforming and filtering signals:
 * - map: transform emitted values
 * - filter: conditionally forward emissions
 * - debounce: suppress rapid emissions
 *
 * Operators are composable using pipe syntax:
 *   auto result = sig | rx::map(f) | rx::filter(pred);
 */

#include "../signal.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

namespace sigslot::rx {

// Forward declarations
template <typename Source, typename F>
class mapped_signal;

template <typename Source, typename Pred>
class filtered_signal;

template <typename Source>
class debounced_signal;

namespace detail {

/**
 * @brief Type trait to extract signal argument types
 */
template <typename T>
struct signal_traits;

template <typename... Args>
struct signal_traits<signal<Args...>> {
    using signal_type = signal<Args...>;
    template <typename F>
    using mapped_result = std::invoke_result_t<F, Args...>;
};

template <typename Source, typename F>
struct signal_traits<mapped_signal<Source, F>> {
    using source_traits = signal_traits<Source>;
    using result = typename source_traits::template mapped_result<F>;
    using signal_type = signal<result>;
    template <typename G>
    using mapped_result = std::invoke_result_t<G, result>;
};

template <typename Source, typename Pred>
struct signal_traits<filtered_signal<Source, Pred>> : signal_traits<Source> {};

template <typename Source>
struct signal_traits<debounced_signal<Source>> : signal_traits<Source> {};

} // namespace detail

/**
 * @brief A signal wrapper that transforms emitted values
 */
template <typename Source, typename F>
class mapped_signal {
public:
    using source_type = Source;
    using transform_type = F;
    using result_type = typename detail::signal_traits<Source>::template mapped_result<F>;

private:
    Source* source_;
    F transform_;
    mutable signal<result_type> output_;
    mutable std::optional<scoped_connection> conn_;

public:
    mapped_signal(Source& source, F transform)
        : source_(&source)
        , transform_(std::move(transform)) {}

    mapped_signal(mapped_signal&& other) noexcept
        : source_(other.source_)
        , transform_(std::move(other.transform_))
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    mapped_signal& operator=(mapped_signal&&) = delete;
    mapped_signal(const mapped_signal&) = delete;
    mapped_signal& operator=(const mapped_signal&) = delete;

    /**
     * @brief Connect a slot to receive transformed values
     */
    template <typename... SlotArgs>
    connection connect(SlotArgs&&... args) {
        ensure_connected();
        return output_.connect(std::forward<SlotArgs>(args)...);
    }

    /**
     * @brief Get the underlying output signal for further composition
     */
    signal<result_type>& output() {
        ensure_connected();
        return output_;
    }

private:
    void ensure_connected() const {
        if (!conn_) {
            conn_.emplace(source_->connect([this](auto&&... args) {
                output_(std::invoke(transform_, std::forward<decltype(args)>(args)...));
            }));
        }
    }
};

/**
 * @brief A signal wrapper that filters emissions based on a predicate
 */
template <typename Source, typename Pred>
class filtered_signal {
public:
    using source_type = Source;
    using predicate_type = Pred;
    using output_signal_type = typename detail::signal_traits<Source>::signal_type;

private:
    Source* source_;
    Pred predicate_;
    mutable output_signal_type output_;
    mutable std::optional<scoped_connection> conn_;

    // Helper to get the actual signal type from source
    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    filtered_signal(Source& source, Pred predicate)
        : source_(&source)
        , predicate_(std::move(predicate)) {}

    filtered_signal(filtered_signal&& other) noexcept
        : source_(other.source_)
        , predicate_(std::move(other.predicate_))
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    filtered_signal& operator=(filtered_signal&&) = delete;
    filtered_signal(const filtered_signal&) = delete;
    filtered_signal& operator=(const filtered_signal&) = delete;

    /**
     * @brief Connect a slot to receive filtered values
     */
    template <typename... SlotArgs>
    connection connect(SlotArgs&&... args) {
        ensure_connected();
        return output_.connect(std::forward<SlotArgs>(args)...);
    }

    /**
     * @brief Get the underlying output signal
     */
    auto& output() {
        ensure_connected();
        return output_;
    }

private:
    void ensure_connected() const {
        if (!conn_) {
            auto& connectable = get_connectable(*source_);
            conn_.emplace(connectable.connect([this](auto&&... args) {
                if (std::invoke(predicate_, args...)) {
                    output_(std::forward<decltype(args)>(args)...);
                }
            }));
        }
    }
};

/**
 * @brief A signal wrapper that debounces emissions
 *
 * Only emits a value after no new values have arrived for the specified duration.
 */
template <typename Source>
class debounced_signal {
public:
    using source_type = Source;
    using clock_type = std::chrono::steady_clock;
    using duration_type = std::chrono::milliseconds;

private:
    struct state {
        std::mutex mtx;
        std::optional<std::tuple<>> pending_value; // Placeholder - actual type determined by source
        clock_type::time_point last_emission;
        bool thread_running = false;
        bool stopped = false;
    };

    Source* source_;
    duration_type delay_;
    mutable std::shared_ptr<state> state_;
    mutable signal<> output_; // Simplified for now - void signal
    mutable std::optional<scoped_connection> conn_;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    debounced_signal(Source& source, duration_type delay)
        : source_(&source)
        , delay_(delay)
        , state_(std::make_shared<state>()) {}

    ~debounced_signal() {
        if (state_) {
            std::lock_guard lock(state_->mtx);
            state_->stopped = true;
        }
    }

    debounced_signal(debounced_signal&& other) noexcept
        : source_(other.source_)
        , delay_(other.delay_)
        , state_(std::move(other.state_))
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    debounced_signal& operator=(debounced_signal&&) = delete;
    debounced_signal(const debounced_signal&) = delete;
    debounced_signal& operator=(const debounced_signal&) = delete;

    /**
     * @brief Connect a slot to receive debounced emissions
     */
    template <typename... SlotArgs>
    connection connect(SlotArgs&&... args) {
        ensure_connected();
        return output_.connect(std::forward<SlotArgs>(args)...);
    }

    /**
     * @brief Get the underlying output signal
     */
    auto& output() {
        ensure_connected();
        return output_;
    }

private:
    void ensure_connected() const {
        if (!conn_) {
            auto& connectable = get_connectable(*source_);
            auto s = state_;
            auto d = delay_;
            auto* out = &output_;

            conn_.emplace(connectable.connect([s, d, out](auto&&...) {
                std::lock_guard lock(s->mtx);
                s->last_emission = clock_type::now();

                if (!s->thread_running) {
                    s->thread_running = true;
                    std::thread([s, d, out]() {
                        while (true) {
                            std::this_thread::sleep_for(d);

                            std::lock_guard lock(s->mtx);
                            if (s->stopped) {
                                s->thread_running = false;
                                return;
                            }

                            auto elapsed = clock_type::now() - s->last_emission;
                            if (elapsed >= d) {
                                (*out)();
                                s->thread_running = false;
                                return;
                            }
                        }
                    }).detach();
                }
            }));
        }
    }
};

// =============================================================================
// Operator factories (for pipe syntax)
// =============================================================================

/**
 * @brief Factory for map operator
 */
template <typename F>
struct map_op {
    F transform;

    template <typename Source>
    auto operator()(Source& source) const {
        return mapped_signal<Source, F>(source, transform);
    }
};

/**
 * @brief Factory for filter operator
 */
template <typename Pred>
struct filter_op {
    Pred predicate;

    template <typename Source>
    auto operator()(Source& source) const {
        return filtered_signal<Source, Pred>(source, predicate);
    }
};

/**
 * @brief Factory for debounce operator
 */
struct debounce_op {
    std::chrono::milliseconds delay;

    template <typename Source>
    auto operator()(Source& source) const {
        return debounced_signal<Source>(source, delay);
    }
};

// =============================================================================
// Public API
// =============================================================================

/**
 * @brief Transform signal values using a function
 *
 * @param transform Function to apply to each emitted value
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto doubled = sig | rx::map([](int x) { return x * 2; });
 */
template <typename F>
auto map(F&& transform) {
    return map_op<std::decay_t<F>>{std::forward<F>(transform)};
}

/**
 * @brief Filter signal emissions based on a predicate
 *
 * @param predicate Function that returns true for values to keep
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto positive = sig | rx::filter([](int x) { return x > 0; });
 */
template <typename Pred>
auto filter(Pred&& predicate) {
    return filter_op<std::decay_t<Pred>>{std::forward<Pred>(predicate)};
}

/**
 * @brief Debounce signal emissions
 *
 * Only emits after the specified duration has passed with no new emissions.
 *
 * @param delay Duration to wait before emitting
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto debounced = sig | rx::debounce(100ms);
 */
inline auto debounce(std::chrono::milliseconds delay) {
    return debounce_op{delay};
}

// =============================================================================
// Pipe operator
// =============================================================================

/**
 * @brief Pipe operator for composing signal transformations
 */
template <typename Source, typename Op>
auto operator|(Source& source, Op&& op) {
    return std::forward<Op>(op)(source);
}

} // namespace sigslot::rx

// =============================================================================
// Execution-aware operators (requires stdexec)
// =============================================================================

#if defined(SIGSLOT_HAVE_STDEXEC)

#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>

namespace sigslot::rx {

/**
 * @brief A signal wrapper that forwards emissions on a specific scheduler
 */
template <typename Source, typename Scheduler>
class observed_signal {
public:
    using source_type = Source;
    using scheduler_type = Scheduler;
    using output_signal_type = typename detail::signal_traits<Source>::signal_type;

private:
    Source* source_;
    Scheduler scheduler_;
    mutable output_signal_type output_;
    mutable std::optional<scoped_connection> conn_;
    mutable exec::async_scope scope_;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    observed_signal(Source& source, Scheduler sched)
        : source_(&source)
        , scheduler_(std::move(sched)) {}

    ~observed_signal() {
        stdexec::sync_wait(scope_.on_empty());
    }

    observed_signal(observed_signal&& other) noexcept
        : source_(other.source_)
        , scheduler_(std::move(other.scheduler_))
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    observed_signal& operator=(observed_signal&&) = delete;
    observed_signal(const observed_signal&) = delete;
    observed_signal& operator=(const observed_signal&) = delete;

    template <typename... SlotArgs>
    connection connect(SlotArgs&&... args) {
        ensure_connected();
        return output_.connect(std::forward<SlotArgs>(args)...);
    }

    auto& output() {
        ensure_connected();
        return output_;
    }

private:
    void ensure_connected() const {
        if (!conn_) {
            auto& connectable = get_connectable(*source_);
            conn_.emplace(connectable.connect([this](auto&&... args) {
                // Capture args by value for async execution
                auto captured = std::make_tuple(args...);
                auto work = stdexec::schedule(scheduler_) 
                    | stdexec::then([this, captured = std::move(captured)]() mutable {
                        std::apply([this](auto&&... a) {
                            output_(std::forward<decltype(a)>(a)...);
                        }, std::move(captured));
                    });
                scope_.spawn(std::move(work));
            }));
        }
    }
};

/**
 * @brief A signal wrapper that debounces using a scheduler
 */
template <typename Source, typename Scheduler>
class scheduler_debounced_signal {
public:
    using source_type = Source;
    using scheduler_type = Scheduler;
    using clock_type = std::chrono::steady_clock;
    using duration_type = std::chrono::milliseconds;

private:
    struct state {
        std::mutex mtx;
        clock_type::time_point last_emission;
        bool pending = false;
    };

    Source* source_;
    Scheduler scheduler_;
    duration_type delay_;
    mutable signal<> output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::shared_ptr<state> state_;
    mutable exec::async_scope scope_;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    scheduler_debounced_signal(Source& source, Scheduler sched, duration_type delay)
        : source_(&source)
        , scheduler_(std::move(sched))
        , delay_(delay)
        , state_(std::make_shared<state>()) {}

    ~scheduler_debounced_signal() {
        stdexec::sync_wait(scope_.on_empty());
    }

    scheduler_debounced_signal(scheduler_debounced_signal&& other) noexcept
        : source_(other.source_)
        , scheduler_(std::move(other.scheduler_))
        , delay_(other.delay_)
        , state_(std::move(other.state_))
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    scheduler_debounced_signal& operator=(scheduler_debounced_signal&&) = delete;
    scheduler_debounced_signal(const scheduler_debounced_signal&) = delete;
    scheduler_debounced_signal& operator=(const scheduler_debounced_signal&) = delete;

    template <typename... SlotArgs>
    connection connect(SlotArgs&&... args) {
        ensure_connected();
        return output_.connect(std::forward<SlotArgs>(args)...);
    }

    auto& output() {
        ensure_connected();
        return output_;
    }

private:
    void ensure_connected() const {
        if (!conn_) {
            auto& connectable = get_connectable(*source_);
            auto s = state_;
            auto d = delay_;
            auto* out = &output_;
            auto* sc = &scope_;
            auto sched = scheduler_;

            conn_.emplace(connectable.connect([s, d, out, sc, sched](auto&&...) mutable {
                std::lock_guard lock(s->mtx);
                s->last_emission = clock_type::now();

                if (!s->pending) {
                    s->pending = true;
                    
                    // Schedule debounce check
                    auto work = stdexec::schedule(sched)
                        | stdexec::then([s, d, out]() {
                            // Simple polling approach - sleep then check
                            std::this_thread::sleep_for(d);
                            
                            std::lock_guard lock(s->mtx);
                            auto elapsed = clock_type::now() - s->last_emission;
                            if (elapsed >= d) {
                                s->pending = false;
                                (*out)();
                            } else {
                                s->pending = false;
                            }
                        });
                    sc->spawn(std::move(work));
                }
            }));
        }
    }
};

// =============================================================================
// Operator factories for execution-aware operators
// =============================================================================

/**
 * @brief Factory for observe_on operator
 */
template <typename Scheduler>
struct observe_on_op {
    Scheduler scheduler;

    template <typename Source>
    auto operator()(Source& source) const {
        return observed_signal<Source, Scheduler>(source, scheduler);
    }
};

/**
 * @brief Factory for debounce_on operator
 */
template <typename Scheduler>
struct debounce_on_op {
    Scheduler scheduler;
    std::chrono::milliseconds delay;

    template <typename Source>
    auto operator()(Source& source) const {
        return scheduler_debounced_signal<Source, Scheduler>(source, scheduler, delay);
    }
};

// =============================================================================
// Public API for execution-aware operators
// =============================================================================

/**
 * @brief Switch execution context for downstream signal processing
 *
 * @param scheduler The scheduler to run downstream operations on
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto on_pool = sig | rx::observe_on(pool.get_scheduler());
 */
template <typename Scheduler>
auto observe_on(Scheduler&& scheduler) {
    return observe_on_op<std::decay_t<Scheduler>>{std::forward<Scheduler>(scheduler)};
}

/**
 * @brief Debounce signal emissions using a scheduler
 *
 * Only emits after the specified duration has passed with no new emissions.
 * Uses the provided scheduler for timing instead of detached threads.
 *
 * @param scheduler The scheduler to use for timing
 * @param delay Duration to wait before emitting
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto debounced = sig | rx::debounce_on(pool.get_scheduler(), 100ms);
 */
template <typename Scheduler>
auto debounce_on(Scheduler&& scheduler, std::chrono::milliseconds delay) {
    return debounce_on_op<std::decay_t<Scheduler>>{std::forward<Scheduler>(scheduler), delay};
}

} // namespace sigslot::rx

#endif // SIGSLOT_HAVE_STDEXEC
