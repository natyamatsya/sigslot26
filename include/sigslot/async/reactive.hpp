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
#include <queue>
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

                            std::lock_guard inner_lock(s->mtx);
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

/**
 * @brief A signal wrapper that throttles emissions
 *
 * Emits the first value, then ignores subsequent values for the specified duration.
 */
template <typename Source>
class throttled_signal {
public:
    using source_type = Source;
    using clock_type = std::chrono::steady_clock;
    using duration_type = std::chrono::milliseconds;
    using output_signal_type = typename detail::signal_traits<Source>::signal_type;

private:
    Source* source_;
    duration_type interval_;
    mutable output_signal_type output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::mutex mtx_;
    mutable clock_type::time_point last_emission_{};

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    throttled_signal(Source& source, duration_type interval)
        : source_(&source)
        , interval_(interval) {}

    throttled_signal(throttled_signal&& other) noexcept
        : source_(other.source_)
        , interval_(other.interval_)
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    throttled_signal& operator=(throttled_signal&&) = delete;
    throttled_signal(const throttled_signal&) = delete;
    throttled_signal& operator=(const throttled_signal&) = delete;

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
                std::lock_guard lock(mtx_);
                auto now = clock_type::now();
                if (now - last_emission_ >= interval_) {
                    last_emission_ = now;
                    output_(std::forward<decltype(args)>(args)...);
                }
            }));
        }
    }
};

/**
 * @brief A signal wrapper that only emits when value changes
 */
template <typename Source>
class distinct_signal {
public:
    using source_type = Source;
    using output_signal_type = typename detail::signal_traits<Source>::signal_type;

private:
    Source* source_;
    mutable output_signal_type output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::mutex mtx_;
    mutable std::optional<std::tuple<>> last_value_; // Placeholder

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    explicit distinct_signal(Source& source)
        : source_(&source) {}

    distinct_signal(distinct_signal&& other) noexcept
        : source_(other.source_)
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    distinct_signal& operator=(distinct_signal&&) = delete;
    distinct_signal(const distinct_signal&) = delete;
    distinct_signal& operator=(const distinct_signal&) = delete;

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
                std::lock_guard lock(mtx_);
                auto current = std::make_tuple(args...);
                using tuple_type = decltype(current);
                
                auto* last = reinterpret_cast<std::optional<tuple_type>*>(&last_value_);
                if (!last->has_value() || *last != current) {
                    *last = current;
                    output_(std::forward<decltype(args)>(args)...);
                }
            }));
        }
    }
};

/**
 * @brief A signal wrapper that only emits when predicate says values differ
 */
template <typename Source, typename Pred>
class distinct_until_changed_signal {
public:
    using source_type = Source;
    using predicate_type = Pred;
    using output_signal_type = typename detail::signal_traits<Source>::signal_type;

private:
    Source* source_;
    Pred predicate_;
    mutable output_signal_type output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::mutex mtx_;
    mutable std::optional<std::tuple<>> last_value_;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    distinct_until_changed_signal(Source& source, Pred predicate)
        : source_(&source)
        , predicate_(std::move(predicate)) {}

    distinct_until_changed_signal(distinct_until_changed_signal&& other) noexcept
        : source_(other.source_)
        , predicate_(std::move(other.predicate_))
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    distinct_until_changed_signal& operator=(distinct_until_changed_signal&&) = delete;
    distinct_until_changed_signal(const distinct_until_changed_signal&) = delete;
    distinct_until_changed_signal& operator=(const distinct_until_changed_signal&) = delete;

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
                std::lock_guard lock(mtx_);
                auto current = std::make_tuple(args...);
                using tuple_type = decltype(current);
                
                auto* last = reinterpret_cast<std::optional<tuple_type>*>(&last_value_);
                if (!last->has_value() || !std::apply([this, &current](auto&&... prev) {
                    return std::apply([this, &prev...](auto&&... curr) {
                        return predicate_(prev..., curr...);
                    }, current);
                }, *last)) {
                    *last = current;
                    output_(std::forward<decltype(args)>(args)...);
                }
            }));
        }
    }
};

/**
 * @brief Factory for throttle operator
 */
struct throttle_op {
    std::chrono::milliseconds interval;

    template <typename Source>
    auto operator()(Source& source) const {
        return throttled_signal<Source>(source, interval);
    }
};

/**
 * @brief Factory for distinct operator
 */
struct distinct_op {
    template <typename Source>
    auto operator()(Source& source) const {
        return distinct_signal<Source>(source);
    }
};

/**
 * @brief Factory for distinct_until_changed operator
 */
template <typename Pred>
struct distinct_until_changed_op {
    Pred predicate;

    template <typename Source>
    auto operator()(Source& source) const {
        return distinct_until_changed_signal<Source, Pred>(source, predicate);
    }
};

// =============================================================================
// Phase 2: Scan, Buffer, Take, Skip
// =============================================================================

/**
 * @brief A signal wrapper that computes a running accumulation
 */
template <typename Source, typename T, typename F>
class scanned_signal {
public:
    using source_type = Source;
    using accumulator_type = T;
    using function_type = F;

private:
    Source* source_;
    T init_;
    F func_;
    mutable signal<T> output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::mutex mtx_;
    mutable T acc_;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    scanned_signal(Source& source, T init, F func)
        : source_(&source)
        , init_(std::move(init))
        , func_(std::move(func))
        , acc_(init_) {}

    scanned_signal(scanned_signal&& other) noexcept
        : source_(other.source_)
        , init_(std::move(other.init_))
        , func_(std::move(other.func_))
        , acc_(std::move(other.acc_))
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    scanned_signal& operator=(scanned_signal&&) = delete;
    scanned_signal(const scanned_signal&) = delete;
    scanned_signal& operator=(const scanned_signal&) = delete;

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
                std::lock_guard lock(mtx_);
                acc_ = func_(acc_, std::forward<decltype(args)>(args)...);
                output_(acc_);
            }));
        }
    }
};

/**
 * @brief A signal wrapper that buffers N emissions then emits as vector
 */
template <typename Source, typename T>
class buffered_signal {
public:
    using source_type = Source;
    using value_type = T;

private:
    Source* source_;
    std::size_t count_;
    mutable signal<std::vector<T>> output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::mutex mtx_;
    mutable std::vector<T> buffer_;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    buffered_signal(Source& source, std::size_t count)
        : source_(&source)
        , count_(count) {
        buffer_.reserve(count);
    }

    buffered_signal(buffered_signal&& other) noexcept
        : source_(other.source_)
        , count_(other.count_)
        , buffer_(std::move(other.buffer_))
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    buffered_signal& operator=(buffered_signal&&) = delete;
    buffered_signal(const buffered_signal&) = delete;
    buffered_signal& operator=(const buffered_signal&) = delete;

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
            conn_.emplace(connectable.connect([this](auto&& arg) {
                std::lock_guard lock(mtx_);
                buffer_.push_back(std::forward<decltype(arg)>(arg));
                if (buffer_.size() >= count_) {
                    output_(std::move(buffer_));
                    buffer_.clear();
                    buffer_.reserve(count_);
                }
            }));
        }
    }
};

/**
 * @brief A signal wrapper that only forwards first N emissions
 */
template <typename Source>
class take_signal {
public:
    using source_type = Source;
    using output_signal_type = typename detail::signal_traits<Source>::signal_type;

private:
    Source* source_;
    std::size_t count_;
    mutable output_signal_type output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::mutex mtx_;
    mutable std::size_t remaining_;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    take_signal(Source& source, std::size_t count)
        : source_(&source)
        , count_(count)
        , remaining_(count) {}

    take_signal(take_signal&& other) noexcept
        : source_(other.source_)
        , count_(other.count_)
        , remaining_(other.remaining_)
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    take_signal& operator=(take_signal&&) = delete;
    take_signal(const take_signal&) = delete;
    take_signal& operator=(const take_signal&) = delete;

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
                std::lock_guard lock(mtx_);
                if (remaining_ > 0) {
                    --remaining_;
                    output_(std::forward<decltype(args)>(args)...);
                    if (remaining_ == 0) {
                        conn_->disconnect();
                    }
                }
            }));
        }
    }
};

/**
 * @brief A signal wrapper that skips first N emissions
 */
template <typename Source>
class skip_signal {
public:
    using source_type = Source;
    using output_signal_type = typename detail::signal_traits<Source>::signal_type;

private:
    Source* source_;
    std::size_t count_;
    mutable output_signal_type output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::mutex mtx_;
    mutable std::size_t remaining_;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    skip_signal(Source& source, std::size_t count)
        : source_(&source)
        , count_(count)
        , remaining_(count) {}

    skip_signal(skip_signal&& other) noexcept
        : source_(other.source_)
        , count_(other.count_)
        , remaining_(other.remaining_)
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    skip_signal& operator=(skip_signal&&) = delete;
    skip_signal(const skip_signal&) = delete;
    skip_signal& operator=(const skip_signal&) = delete;

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
                std::lock_guard lock(mtx_);
                if (remaining_ > 0) {
                    --remaining_;
                } else {
                    output_(std::forward<decltype(args)>(args)...);
                }
            }));
        }
    }
};

/**
 * @brief A signal wrapper that forwards while predicate is true
 */
template <typename Source, typename Pred>
class take_while_signal {
public:
    using source_type = Source;
    using predicate_type = Pred;
    using output_signal_type = typename detail::signal_traits<Source>::signal_type;

private:
    Source* source_;
    Pred predicate_;
    mutable output_signal_type output_;
    mutable std::optional<scoped_connection> conn_;
    mutable std::mutex mtx_;
    mutable bool stopped_ = false;

    template <typename S>
    static auto& get_connectable(S& s) {
        if constexpr (requires { s.output(); }) {
            return s.output();
        } else {
            return s;
        }
    }

public:
    take_while_signal(Source& source, Pred predicate)
        : source_(&source)
        , predicate_(std::move(predicate)) {}

    take_while_signal(take_while_signal&& other) noexcept
        : source_(other.source_)
        , predicate_(std::move(other.predicate_))
        , stopped_(other.stopped_)
        , output_(std::move(other.output_)) {
        other.conn_.reset();
        ensure_connected();
    }

    take_while_signal& operator=(take_while_signal&&) = delete;
    take_while_signal(const take_while_signal&) = delete;
    take_while_signal& operator=(const take_while_signal&) = delete;

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
                std::lock_guard lock(mtx_);
                if (!stopped_ && predicate_(args...)) {
                    output_(std::forward<decltype(args)>(args)...);
                } else {
                    stopped_ = true;
                    conn_->disconnect();
                }
            }));
        }
    }
};

// Operator factories for Phase 2

template <typename T, typename F>
struct scan_op {
    T init;
    F func;

    template <typename Source>
    auto operator()(Source& source) const {
        return scanned_signal<Source, T, F>(source, init, func);
    }
};

template <typename T>
struct buffer_op {
    std::size_t count;

    template <typename Source>
    auto operator()(Source& source) const {
        return buffered_signal<Source, T>(source, count);
    }
};

struct take_op {
    std::size_t count;

    template <typename Source>
    auto operator()(Source& source) const {
        return take_signal<Source>(source, count);
    }
};

struct skip_op {
    std::size_t count;

    template <typename Source>
    auto operator()(Source& source) const {
        return skip_signal<Source>(source, count);
    }
};

template <typename Pred>
struct take_while_op {
    Pred predicate;

    template <typename Source>
    auto operator()(Source& source) const {
        return take_while_signal<Source, Pred>(source, predicate);
    }
};

// =============================================================================
// Phase 4: Multi-signal operators (merge, combine_latest, zip)
// =============================================================================

// Note: merged_signal for heterogeneous signals removed - use merged_typed_signal instead

// Forward declaration
template <typename T>
class merged_typed_signal;

// Signal traits for merged_typed_signal
template <typename T>
struct detail::signal_traits<merged_typed_signal<T>> {
    using signal_type = signal<T>;
    using tuple_type = std::tuple<T>;
};

/**
 * @brief A signal that merges emissions from signals with same arg type
 */
template <typename T>
class merged_typed_signal {
public:
    using value_type = T;

private:
    std::vector<signal<T>*> sources_;
    mutable signal<T> output_;
    mutable std::vector<scoped_connection> conns_;

public:
    template <typename... Sources>
    explicit merged_typed_signal(Sources&... sources)
        : sources_{&sources...} {}

    merged_typed_signal(merged_typed_signal&&) = default;
    merged_typed_signal& operator=(merged_typed_signal&&) = delete;
    merged_typed_signal(const merged_typed_signal&) = delete;
    merged_typed_signal& operator=(const merged_typed_signal&) = delete;

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
        if (conns_.empty()) {
            for (auto* src : sources_) {
                conns_.emplace_back(src->connect([this](T value) {
                    output_(std::move(value));
                }));
            }
        }
    }
};

/**
 * @brief A signal that combines latest values from two signals
 */
template <typename Sig1, typename Sig2, typename T1, typename T2>
class combined_signal {
public:
    using output_type = std::tuple<T1, T2>;

private:
    Sig1* sig1_;
    Sig2* sig2_;
    mutable signal<T1, T2> output_;
    mutable std::optional<scoped_connection> conn1_;
    mutable std::optional<scoped_connection> conn2_;
    mutable std::mutex mtx_;
    mutable std::optional<T1> last1_;
    mutable std::optional<T2> last2_;

public:
    combined_signal(Sig1& sig1, Sig2& sig2)
        : sig1_(&sig1)
        , sig2_(&sig2) {}

    combined_signal(combined_signal&&) = default;
    combined_signal& operator=(combined_signal&&) = delete;
    combined_signal(const combined_signal&) = delete;
    combined_signal& operator=(const combined_signal&) = delete;

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
        if (!conn1_) {
            conn1_.emplace(sig1_->connect([this](T1 v) {
                std::lock_guard lock(mtx_);
                last1_ = std::move(v);
                if (last2_) {
                    output_(*last1_, *last2_);
                }
            }));
        }
        if (!conn2_) {
            conn2_.emplace(sig2_->connect([this](T2 v) {
                std::lock_guard lock(mtx_);
                last2_ = std::move(v);
                if (last1_) {
                    output_(*last1_, *last2_);
                }
            }));
        }
    }
};

/**
 * @brief A signal that zips emissions from two signals 1:1
 */
template <typename Sig1, typename Sig2, typename T1, typename T2>
class zipped_signal {
public:
    using output_type = std::tuple<T1, T2>;

private:
    Sig1* sig1_;
    Sig2* sig2_;
    mutable signal<T1, T2> output_;
    mutable std::optional<scoped_connection> conn1_;
    mutable std::optional<scoped_connection> conn2_;
    mutable std::mutex mtx_;
    mutable std::queue<T1> queue1_;
    mutable std::queue<T2> queue2_;

public:
    zipped_signal(Sig1& sig1, Sig2& sig2)
        : sig1_(&sig1)
        , sig2_(&sig2) {}

    zipped_signal(zipped_signal&&) = default;
    zipped_signal& operator=(zipped_signal&&) = delete;
    zipped_signal(const zipped_signal&) = delete;
    zipped_signal& operator=(const zipped_signal&) = delete;

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
        if (!conn1_) {
            conn1_.emplace(sig1_->connect([this](T1 v) {
                std::lock_guard lock(mtx_);
                if (!queue2_.empty()) {
                    output_(std::move(v), std::move(queue2_.front()));
                    queue2_.pop();
                } else {
                    queue1_.push(std::move(v));
                }
            }));
        }
        if (!conn2_) {
            conn2_.emplace(sig2_->connect([this](T2 v) {
                std::lock_guard lock(mtx_);
                if (!queue1_.empty()) {
                    output_(std::move(queue1_.front()), std::move(v));
                    queue1_.pop();
                } else {
                    queue2_.push(std::move(v));
                }
            }));
        }
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

/**
 * @brief Throttle signal emissions
 *
 * Emits the first value, then ignores subsequent values for the duration.
 *
 * @param interval Duration to ignore values after each emission
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto throttled = sig | rx::throttle(100ms);
 */
inline auto throttle(std::chrono::milliseconds interval) {
    return throttle_op{interval};
}

/**
 * @brief Only emit when value changes
 *
 * Filters out consecutive duplicate values using operator==.
 *
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto distinct = sig | rx::distinct();
 */
inline auto distinct() {
    return distinct_op{};
}

/**
 * @brief Only emit when predicate says values differ
 *
 * @param predicate Function that returns true if values are equal
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto by_id = sig | rx::distinct_until_changed([](auto& a, auto& b) { 
 *       return a.id == b.id; 
 *   });
 */
template <typename Pred>
auto distinct_until_changed(Pred&& predicate) {
    return distinct_until_changed_op<std::decay_t<Pred>>{std::forward<Pred>(predicate)};
}

/**
 * @brief Running accumulator - emits accumulated value after each input
 *
 * @param init Initial accumulator value
 * @param func Accumulator function (acc, value) -> new_acc
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto running_sum = sig | rx::scan(0, [](int acc, int x) { return acc + x; });
 */
template <typename T, typename F>
auto scan(T&& init, F&& func) {
    return scan_op<std::decay_t<T>, std::decay_t<F>>{
        std::forward<T>(init), std::forward<F>(func)};
}

/**
 * @brief Buffer N emissions then emit as vector
 *
 * @tparam T Value type to buffer
 * @param count Number of emissions to buffer
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto batched = sig | rx::buffer<int>(3);
 */
template <typename T>
auto buffer(std::size_t count) {
    return buffer_op<T>{count};
}

/**
 * @brief Only forward first N emissions
 *
 * @param count Number of emissions to forward
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto first_five = sig | rx::take(5);
 */
inline auto take(std::size_t count) {
    return take_op{count};
}

/**
 * @brief Skip first N emissions, forward the rest
 *
 * @param count Number of emissions to skip
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto after_warmup = sig | rx::skip(3);
 */
inline auto skip(std::size_t count) {
    return skip_op{count};
}

/**
 * @brief Forward while predicate is true, stop on first false
 *
 * @param predicate Function that returns true to continue forwarding
 * @return An operator that can be applied to a signal
 *
 * Example:
 *   auto while_positive = sig | rx::take_while([](int x) { return x > 0; });
 */
template <typename Pred>
auto take_while(Pred&& predicate) {
    return take_while_op<std::decay_t<Pred>>{std::forward<Pred>(predicate)};
}

/**
 * @brief Merge multiple signals with the same argument type
 *
 * @param sources Signals to merge
 * @return A merged signal that emits from any source
 *
 * Example:
 *   auto merged = rx::merge(sig1, sig2, sig3);
 */
template <typename T, typename... Sources>
auto merge(Sources&... sources) {
    return merged_typed_signal<T>(sources...);
}

/**
 * @brief Combine latest values from two signals
 *
 * Emits a pair when either signal fires (after both have fired once).
 *
 * @param sig1 First signal
 * @param sig2 Second signal
 * @return A combined signal emitting (T1, T2)
 *
 * Example:
 *   auto combined = rx::combine_latest<int, std::string>(sig1, sig2);
 */
template <typename T1, typename T2, typename Sig1, typename Sig2>
auto combine_latest(Sig1& sig1, Sig2& sig2) {
    return combined_signal<Sig1, Sig2, T1, T2>(sig1, sig2);
}

/**
 * @brief Zip emissions from two signals 1:1
 *
 * Pairs emissions in order, waiting for both signals.
 *
 * @param sig1 First signal
 * @param sig2 Second signal
 * @return A zipped signal emitting (T1, T2)
 *
 * Example:
 *   auto zipped = rx::zip<int, std::string>(sig1, sig2);
 */
template <typename T1, typename T2, typename Sig1, typename Sig2>
auto zip(Sig1& sig1, Sig2& sig2) {
    return zipped_signal<Sig1, Sig2, T1, T2>(sig1, sig2);
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
        // Disconnect first to prevent new work from being spawned
        conn_.reset();
        // Then wait for any in-flight work to complete
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
                    | stdexec::then([this, c = std::move(captured)]() mutable {
                        std::apply([this](auto&&... a) {
                            output_(std::forward<decltype(a)>(a)...);
                        }, std::move(c));
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
        // Disconnect first to prevent new work from being spawned
        conn_.reset();
        // Then wait for any in-flight work to complete
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
                            
                            std::lock_guard lk(s->mtx);
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
