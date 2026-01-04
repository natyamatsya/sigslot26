// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

#include "slot-variant.hpp"
#include "connection.hpp"
#include <vector>
#include <mutex>
#include <algorithm>

namespace sigslot {

/**
 * @brief A high-performance signal using inline slot storage.
 * 
 * Unlike signal_base which stores slots via shared_ptr (heap allocated),
 * signal_inline stores slot_variant objects directly in a vector for
 * better cache locality and faster emission.
 * 
 * Trade-offs:
 * - Faster emission (~60% for single slot)
 * - Faster connection (~58x)
 * - No support for external connection objects (slots are owned by signal)
 * - Not thread-safe (use for single-threaded scenarios)
 * - Fixed callable size limit (64 bytes default)
 * 
 * @tparam T... Signal argument types
 */
template<typename... T>
class signal_inline {
public:
    using group_id = int32_t;
    using slot_type = detail::slot_variant<group_id, T...>;
    
    signal_inline() = default;
    ~signal_inline() = default;
    
    // Non-copyable
    signal_inline(const signal_inline&) = delete;
    signal_inline& operator=(const signal_inline&) = delete;
    
    // Movable
    signal_inline(signal_inline&&) noexcept = default;
    signal_inline& operator=(signal_inline&&) noexcept = default;
    
    /**
     * @brief Connect a callable (lambda, functor, free function)
     */
    template<typename Callable>
    void connect(Callable&& c, group_id gid = group_id{}) {
        slots_.push_back(slot_type::make_plain(std::forward<Callable>(c), gid));
    }
    
    /**
     * @brief Connect a pointer-to-member-function with object pointer
     */
    template<typename Pmf, typename Ptr>
    void connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        slots_.push_back(slot_type::make_pmf(
            std::forward<Pmf>(pmf), 
            std::forward<Ptr>(ptr), 
            gid
        ));
    }
    
    /**
     * @brief Connect with lifetime tracking via weak_ptr
     */
    template<typename Callable, typename Trackable>
    void connect_tracked(Callable&& c, std::weak_ptr<Trackable> ptr, group_id gid = group_id{}) {
        slots_.push_back(slot_type::make_tracked(
            std::forward<Callable>(c),
            std::move(ptr),
            gid
        ));
    }
    
    /**
     * @brief Emit the signal - calls all connected slots
     */
    template<typename... U>
    void operator()(U&&... args) {
        if (blocked_) return;
        
        for (auto& slot : slots_) {
            slot(std::forward<U>(args)...);
        }
    }
    
    /**
     * @brief Emit and remove expired tracked slots
     */
    template<typename... U>
    void emit_and_cleanup(U&&... args) {
        if (blocked_) return;
        
        auto it = slots_.begin();
        while (it != slots_.end()) {
            if (!(*it)(std::forward<U>(args)...)) {
                // Slot expired (tracked object destroyed)
                it = slots_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    /**
     * @brief Disconnect all slots
     */
    void disconnect_all() {
        slots_.clear();
    }
    
    /**
     * @brief Block/unblock all slots
     */
    void block() noexcept { blocked_ = true; }
    void unblock() noexcept { blocked_ = false; }
    [[nodiscard]] bool blocked() const noexcept { return blocked_; }
    
    /**
     * @brief Get number of connected slots
     */
    [[nodiscard]] std::size_t slot_count() const noexcept {
        return slots_.size();
    }
    
    /**
     * @brief Check if signal has any connected slots
     */
    [[nodiscard]] bool empty() const noexcept {
        return slots_.empty();
    }
    
    /**
     * @brief Reserve capacity for slots (avoid reallocations)
     */
    void reserve(std::size_t n) {
        slots_.reserve(n);
    }

private:
    std::vector<slot_type> slots_;
    bool blocked_ = false;
};

/**
 * @brief Thread-safe version of signal_inline using mutex
 */
template<typename... T>
class signal_inline_safe {
public:
    using group_id = int32_t;
    using slot_type = detail::slot_variant<group_id, T...>;
    
    signal_inline_safe() = default;
    ~signal_inline_safe() = default;
    
    signal_inline_safe(const signal_inline_safe&) = delete;
    signal_inline_safe& operator=(const signal_inline_safe&) = delete;
    signal_inline_safe(signal_inline_safe&&) = delete;
    signal_inline_safe& operator=(signal_inline_safe&&) = delete;
    
    template<typename Callable>
    void connect(Callable&& c, group_id gid = group_id{}) {
        std::lock_guard lock(mutex_);
        slots_.push_back(slot_type::make_plain(std::forward<Callable>(c), gid));
    }
    
    template<typename Pmf, typename Ptr>
    void connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        std::lock_guard lock(mutex_);
        slots_.push_back(slot_type::make_pmf(
            std::forward<Pmf>(pmf), 
            std::forward<Ptr>(ptr), 
            gid
        ));
    }
    
    template<typename... U>
    void operator()(U&&... args) {
        std::lock_guard lock(mutex_);
        if (blocked_) return;
        
        for (auto& slot : slots_) {
            slot(std::forward<U>(args)...);
        }
    }
    
    void disconnect_all() {
        std::lock_guard lock(mutex_);
        slots_.clear();
    }
    
    void block() noexcept { blocked_ = true; }
    void unblock() noexcept { blocked_ = false; }
    
    [[nodiscard]] std::size_t slot_count() const {
        std::lock_guard lock(mutex_);
        return slots_.size();
    }

private:
    std::vector<slot_type> slots_;
    mutable std::mutex mutex_;
    std::atomic<bool> blocked_{false};
};

} // namespace sigslot
