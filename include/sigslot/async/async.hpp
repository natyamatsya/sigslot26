// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: mousebyte/sigslot20 contributors
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once
#include "../signal.hpp"
#include "coroutine.hpp"
#include <tuple>
#include <optional>
#include <chrono>

/**
 * @brief Async extensions for sigslot26
 * 
 * Provides:
 * - Coroutine support (C++20) for awaitable signals
 * - Generator for async ranges
 * - std::execution integration (when SIGSLOT_HAVE_STDEXEC is defined)
 */

namespace sigslot::async {

/**
 * @brief Simple generator implementation for C++20
 * (std::generator is C++23, so we provide our own)
 */
template<typename T>
class generator {
public:
    struct promise_type {
        T current_value;
        std::exception_ptr exception;

        generator get_return_object() {
            return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T value) {
            current_value = std::move(value);
            return {};
        }

        void return_void() {}

        void unhandled_exception() { exception = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    struct iterator {
        handle_type handle;

        iterator(handle_type h)
            : handle(h) {}

        iterator& operator++() {
            handle.resume();
            return *this;
        }

        T& operator*() const { return handle.promise().current_value; }

        bool operator==(std::default_sentinel_t) const { return handle.done(); }
    };

    generator(handle_type h)
        : handle(h) {}

    generator(generator&& other) noexcept
        : handle(other.handle) {
        other.handle = nullptr;
    }

    ~generator() {
        if (handle)
            handle.destroy();
    }

    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;

    iterator begin() {
        if (handle)
            handle.resume();
        return iterator{handle};
    }

    std::default_sentinel_t end() { return {}; }

private:
    handle_type handle;
};

/**
 * @brief Awaitable wrapper for signal_base
 * Enables co_await on signals
 */
template<GroupId Group, typename Lockable, typename... T>
class awaitable_signal_wrapper {
public:
    using signal_type = signal_base<Group, Lockable, T...>;
    using value_type = std::tuple<T...>;
    using connection = sigslot::connection;

    awaitable_signal_wrapper(signal_type* sig)
        : signal(sig) {}

    // Make signal awaitable
    auto operator co_await() { return simple_signal_awaiter<awaitable_signal_wrapper>{this}; }

    // Await with timeout
    template<typename Duration>
    auto next_or_timeout(Duration timeout) {
        return signal_awaiter<awaitable_signal_wrapper, Duration>{this, timeout};
    }

    // Connect method for awaiter
    template<typename Func>
    connection connect(Func&& f) {
        return signal->connect(std::forward<Func>(f));
    }

    // Async range - infinite generator of signal emissions
    generator<value_type> async_range() {
        while (true) {
            co_yield co_await *this;
        }
    }

    // Take N emissions
    generator<value_type> take(size_t n) {
        for (size_t i = 0; i < n; ++i) {
            co_yield co_await *this;
        }
    }

    // Filter emissions based on predicate
    template<typename Predicate>
    generator<value_type> filter(Predicate&& pred) {
        while (true) {
            auto value = co_await *this;
            if (std::apply(pred, value))
                co_yield value;
        }
    }

    // Transform emissions
    template<typename Func>
    auto transform(Func&& f) -> generator<decltype(std::apply(f, std::declval<value_type>()))> {
        while (true) {
            auto value = co_await *this;
            co_yield std::apply(f, value);
        }
    }

private:
    signal_type* signal;
};

/**
 * @brief Make a signal awaitable
 @return a wrapper that can be co_await'ed
 */
template<GroupId Group, typename Lockable, typename... T>
auto make_awaitable(signal_base<Group, Lockable, T...>& sig) {
    return awaitable_signal_wrapper<Group, Lockable, T...>{&sig};
}

/**
 * @brief Await signal with timeout
 */
template<typename Duration, GroupId Group, typename Lockable, typename... T>
auto await_with_timeout(signal_base<Group, Lockable, T...>& sig, Duration timeout) {
    return awaitable_signal_wrapper<Group, Lockable, T...>{&sig}.next_or_timeout(timeout);
}

} // namespace sigslot::async
