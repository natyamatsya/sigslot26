// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: mousebyte/sigslot20 contributors
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once
#include <coroutine>
#include <optional>
#include <chrono>
#include <exception>

namespace sigslot::async {

/**
 * @brief Simple task type for coroutine slots
 * Provides RAII management of coroutine handles
 */
template<typename T>
class task {
public:
    struct promise_type {
        T value;
        std::exception_ptr exception;

        task get_return_object() {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(T v) { value = std::move(v); }

        void unhandled_exception() { exception = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    task(handle_type h)
        : handle(h) {}

    task(task&& other) noexcept
        : handle(other.handle) {
        other.handle = nullptr;
    }

    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle)
                handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    ~task() {
        if (handle)
            handle.destroy();
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    T get() {
        if (handle.promise().exception)
            std::rethrow_exception(handle.promise().exception);
        return std::move(handle.promise().value);
    }

private:
    handle_type handle;
};

// Specialization for void
template<>
class task<void> {
public:
    struct promise_type {
        std::exception_ptr exception;

        task get_return_object() {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_void() {}

        void unhandled_exception() { exception = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    task(handle_type h)
        : handle(h) {}

    task(task&& other) noexcept
        : handle(other.handle) {
        other.handle = nullptr;
    }

    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle)
                handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    ~task() {
        if (handle)
            handle.destroy();
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    void get() {
        if (handle.promise().exception)
            std::rethrow_exception(handle.promise().exception);
    }

private:
    handle_type handle;
};

/**
 * @brief Awaiter for signal emissions with timeout support
 * 
 * @tparam Signal A signal type with value_type and connection typedefs
 * @tparam Duration The timeout duration type
 */
template<typename Signal, typename Duration = std::chrono::milliseconds>
class signal_awaiter {
public:
    using value_type = typename Signal::value_type;

    signal_awaiter(Signal* sig, std::optional<Duration> timeout = std::nullopt)
        : signal_(sig)
        , timeout_duration_(timeout) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) {
        continuation_ = h;

        // One-shot connection that captures the value and resumes
        conn_ = signal_->connect([this](auto&&... args) {
            result_ = std::make_tuple(std::forward<decltype(args)>(args)...);
            conn_.disconnect();

            if (timeout_active_)
                timeout_active_ = false;

            if (continuation_)
                continuation_.resume();
        });

        // Start timeout if specified
        if (timeout_duration_) {
            timeout_active_ = true;
            timeout_thread_ = std::thread([this, dur = *timeout_duration_]() {
                std::this_thread::sleep_for(dur);
                if (timeout_active_) {
                    timeout_active_ = false;
                    conn_.disconnect();
                    timed_out_ = true;
                    if (continuation_)
                        continuation_.resume();
                }
            });
        }
    }

    auto await_resume() {
        if (timeout_thread_.joinable())
            timeout_thread_.join();

        if (timed_out_)
            return std::optional<value_type>{};

        return std::optional<value_type>{std::move(result_)};
    }

private:
    Signal* signal_;
    std::optional<Duration> timeout_duration_;
    std::optional<value_type> result_;
    typename Signal::connection conn_;
    std::coroutine_handle<> continuation_;
    std::thread timeout_thread_;
    std::atomic<bool> timeout_active_{false};
    bool timed_out_{false};
};

/**
 * @brief Simple awaiter without timeout
 * 
 * @tparam Signal A signal type with value_type and connection typedefs
 */
template<typename Signal>
class simple_signal_awaiter {
public:
    using value_type = typename Signal::value_type;

    simple_signal_awaiter(Signal* sig)
        : signal_(sig) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) {
        continuation_ = h;

        // One-shot connection - capture result pointer to avoid 'this' access after resume
        conn_ = signal_->connect([this, result_ptr = &result_](auto&&... args) {
            // Store result first
            *result_ptr = std::make_tuple(std::forward<decltype(args)>(args)...);

            // Save what we need before any potential destruction
            auto conn = std::move(conn_);
            auto cont = continuation_;

            // Disconnect first (safe, we have a copy)
            conn.disconnect();

            // Resume coroutine - after this, 'this' may be destroyed
            // DO NOT access 'this' or any member after this point!
            if (cont)
                cont.resume();
        });
    }

    value_type await_resume() { return std::move(*result_); }

private:
    Signal* signal_;
    std::optional<value_type> result_;
    typename Signal::connection conn_;
    std::coroutine_handle<> continuation_;
};

} // namespace sigslot::async
