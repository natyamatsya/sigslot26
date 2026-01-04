// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: palacaze/sigslot contributors
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

#include <atomic>
#include <concepts>
#include <memory>
#include <type_traits>

#ifdef SIGSLOT_USE_INTRUSIVE_PTR
#include "intrusive-ptr.hpp"
#endif

namespace sigslot {

// GroupId concept for signal group ordering (must be defined before slot_state)
template<typename T>
concept GroupId = requires(T g1, T g2) {
    requires std::is_default_constructible_v<T>;
    requires std::is_copy_constructible_v<T>;
    { g1 < g2 } -> std::same_as<bool>;
    { g1 == g2 } -> std::same_as<bool>;
};

// Forward declaration for signal_base
template<GroupId, typename, typename...>
class signal_base;

namespace detail {

/** @brief slot_state holds slot type independent state, to be used to interact with
 * slots indirectly through connection and scoped_connection objects.
 */
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
class slot_state : public intrusive_refcount {
#else
class slot_state : public std::enable_shared_from_this<slot_state> {
#endif
public:
    slot_state() noexcept
        : m_index(0)
        , m_connected(true)
        , m_blocked(false) {}

#ifdef SIGSLOT_USE_INTRUSIVE_PTR
    ~slot_state() override = default;
#else
    virtual ~slot_state() = default;
#endif

    [[nodiscard]] virtual bool connected() const noexcept {
        return m_connected.load(std::memory_order_relaxed);
    }

    bool disconnect() noexcept {
        bool ret = m_connected.exchange(false);
        if (ret) {
            do_disconnect();
        }
        return ret;
    }

    [[nodiscard]] bool blocked() const noexcept {
        return m_blocked.load(std::memory_order_relaxed);
    }
    void block() noexcept { m_blocked.store(true, std::memory_order_relaxed); }
    void unblock() noexcept { m_blocked.store(false, std::memory_order_relaxed); }

    // Index management for signal_base (public for cross-compiler compatibility)
    [[nodiscard]] std::size_t index() const noexcept {
        return m_index.load(std::memory_order_relaxed);
    }

    void set_index(std::size_t idx) noexcept { m_index.store(idx, std::memory_order_relaxed); }

protected:
    virtual void do_disconnect() {}

private:
    std::atomic<std::size_t> m_index;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_blocked;
};

} // namespace detail

// Type aliases for pointer types based on configuration
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
template<typename T>
using slot_weak_ptr = detail::intrusive_weak_ptr<T>;
template<typename T>
using slot_strong_ptr = detail::intrusive_ptr<T>;
#else
template<typename T>
using slot_weak_ptr = std::weak_ptr<T>;
template<typename T>
using slot_strong_ptr = std::shared_ptr<T>;
#endif

/**
 * @brief connection_blocker is a RAII object that blocks a connection until destruction
 */
class connection_blocker {
public:
    connection_blocker() = default;
    ~connection_blocker() noexcept { release(); }

    connection_blocker(const connection_blocker&) = delete;
    connection_blocker& operator=(const connection_blocker&) = delete;

    connection_blocker(connection_blocker&& o) noexcept
        : m_state{std::move(o.m_state)} {}

    connection_blocker& operator=(connection_blocker&& o) noexcept {
        release();
        m_state.swap(o.m_state);
        return *this;
    }

private:
    friend class connection;
    explicit connection_blocker(slot_weak_ptr<detail::slot_state> s) noexcept
        : m_state{std::move(s)} {
        if (auto d = m_state.lock()) {
            d->block();
        }
    }

    void release() noexcept {
        if (auto d = m_state.lock()) {
            d->unblock();
        }
    }

private:
    slot_weak_ptr<detail::slot_state> m_state;
};


/**
 * @brief A connection object allows interaction with an ongoing slot connection
 *
 * It allows common actions such as connection blocking and disconnection.
 * @note that connection is not a RAII object, one does not need to hold one
 * such object to keep the signal-slot connection alive.
 */
class connection {
public:
    connection() = default;
    virtual ~connection() = default;

    connection(const connection&) noexcept = default;
    connection& operator=(const connection&) noexcept = default;
    connection(connection&&) noexcept = default;
    connection& operator=(connection&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept { return !m_state.expired(); }

    [[nodiscard]] bool connected() const noexcept {
        const auto d = m_state.lock();
        return d && d->connected();
    }

    bool disconnect() noexcept {
        auto d = m_state.lock();
        return d && d->disconnect();
    }

    [[nodiscard]] bool blocked() const noexcept {
        const auto d = m_state.lock();
        return d && d->blocked();
    }

    void block() noexcept {
        if (auto d = m_state.lock()) {
            d->block();
        }
    }

    void unblock() noexcept {
        if (auto d = m_state.lock()) {
            d->unblock();
        }
    }

    [[nodiscard]] connection_blocker blocker() const noexcept {
        return connection_blocker{m_state};
    }

    // Constructor from slot state (public for cross-compiler compatibility with concepts)
    explicit connection(slot_weak_ptr<detail::slot_state> s) noexcept
        : m_state{std::move(s)} {}

protected:
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
    slot_weak_ptr<detail::slot_state> m_state;
};

/**
 * @brief scoped_connection is a RAII version of connection
 * It disconnects the slot from the signal upon destruction.
 */
class scoped_connection final : public connection {
public:
    scoped_connection() = default;
    ~scoped_connection() override { disconnect(); }

    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    /*implicit*/ scoped_connection(const connection& c) noexcept
        : connection(c) {}
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    /*implicit*/ scoped_connection(connection&& c) noexcept
        : connection(std::move(c)) {}

    scoped_connection(const scoped_connection&) noexcept = delete;
    scoped_connection& operator=(const scoped_connection&) noexcept = delete;

    scoped_connection(scoped_connection&& o) noexcept
        : connection{std::move(o.m_state)} {}

    scoped_connection& operator=(scoped_connection&& o) noexcept {
        disconnect();
        m_state.swap(o.m_state);
        return *this;
    }

    // Constructor from slot state (public for cross-compiler compatibility with concepts)
    explicit scoped_connection(slot_weak_ptr<detail::slot_state> s) noexcept
        : connection{std::move(s)} {}
};

} // namespace sigslot
