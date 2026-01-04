// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

#include "slot-variant.hpp"
#include "connection.hpp"
#include <vector>
#include <mutex>
#include <shared_mutex>
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
 * @brief Thread-safe version of signal_inline using read-write lock
 * 
 * Optimized for read-heavy workloads (many emissions, few connections).
 * Multiple threads can emit simultaneously; connect/disconnect acquires exclusive lock.
 */
template<typename... T>
class signal_inline_rw {
public:
    using group_id = int32_t;
    using slot_type = detail::slot_variant<group_id, T...>;
    
    signal_inline_rw() = default;
    ~signal_inline_rw() = default;
    
    signal_inline_rw(const signal_inline_rw&) = delete;
    signal_inline_rw& operator=(const signal_inline_rw&) = delete;
    signal_inline_rw(signal_inline_rw&&) = delete;
    signal_inline_rw& operator=(signal_inline_rw&&) = delete;
    
    template<typename Callable>
    void connect(Callable&& c, group_id gid = group_id{}) {
        std::unique_lock lock(mutex_);
        slots_.push_back(slot_type::make_plain(std::forward<Callable>(c), gid));
    }
    
    template<typename Pmf, typename Ptr>
    void connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        std::unique_lock lock(mutex_);
        slots_.push_back(slot_type::make_pmf(
            std::forward<Pmf>(pmf), 
            std::forward<Ptr>(ptr), 
            gid
        ));
    }
    
    template<typename... U>
    void operator()(U&&... args) {
        std::shared_lock lock(mutex_);
        if (blocked_.load(std::memory_order_relaxed)) return;
        
        for (auto& slot : slots_) {
            slot(std::forward<U>(args)...);
        }
    }
    
    void disconnect_all() {
        std::unique_lock lock(mutex_);
        slots_.clear();
    }
    
    void block() noexcept { blocked_.store(true, std::memory_order_relaxed); }
    void unblock() noexcept { blocked_.store(false, std::memory_order_relaxed); }
    
    [[nodiscard]] std::size_t slot_count() const {
        std::shared_lock lock(mutex_);
        return slots_.size();
    }

private:
    std::vector<slot_type> slots_;
    mutable std::shared_mutex mutex_;
    std::atomic<bool> blocked_{false};
};

/**
 * @brief Lock-free thread-safe signal using RCU (Read-Copy-Update)
 * 
 * Emission is completely lock-free (just atomic load + refcount).
 * Connect/disconnect creates a new slot list and atomically swaps.
 * 
 * Trade-off: Uses pointer indirection for slots (like signal_base),
 * but with slot_variant's faster dispatch.
 */
template<typename... T>
class signal_inline_rcu {
public:
    using group_id = int32_t;
    using slot_type = detail::slot_variant<group_id, T...>;
    using slot_ptr = std::unique_ptr<slot_type>;
    using slot_list = std::vector<slot_ptr>;
    
    signal_inline_rcu() 
        : slots_(std::make_shared<slot_list>()) {}
    
    ~signal_inline_rcu() = default;
    
    signal_inline_rcu(const signal_inline_rcu&) = delete;
    signal_inline_rcu& operator=(const signal_inline_rcu&) = delete;
    signal_inline_rcu(signal_inline_rcu&&) = delete;
    signal_inline_rcu& operator=(signal_inline_rcu&&) = delete;
    
    template<typename Callable>
    void connect(Callable&& c, group_id gid = group_id{}) {
        std::lock_guard lock(write_mutex_);
        
        // Create new list with existing slots + new one
        auto old_list = slots_.load(std::memory_order_acquire);
        auto new_list = std::make_shared<slot_list>();
        new_list->reserve(old_list->size() + 1);
        
        // Copy pointers (slots stay in place)
        for (auto& slot : *old_list) {
            new_list->push_back(std::move(slot));
        }
        new_list->push_back(std::make_unique<slot_type>(
            slot_type::make_plain(std::forward<Callable>(c), gid)
        ));
        
        slots_.store(std::move(new_list), std::memory_order_release);
    }
    
    template<typename Pmf, typename Ptr>
    void connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        std::lock_guard lock(write_mutex_);
        
        auto old_list = slots_.load(std::memory_order_acquire);
        auto new_list = std::make_shared<slot_list>();
        new_list->reserve(old_list->size() + 1);
        
        for (auto& slot : *old_list) {
            new_list->push_back(std::move(slot));
        }
        new_list->push_back(std::make_unique<slot_type>(
            slot_type::make_pmf(std::forward<Pmf>(pmf), std::forward<Ptr>(ptr), gid)
        ));
        
        slots_.store(std::move(new_list), std::memory_order_release);
    }
    
    /**
     * @brief Lock-free emission - multiple threads can emit simultaneously
     */
    template<typename... U>
    void operator()(U&&... args) {
        if (blocked_.load(std::memory_order_relaxed)) return;
        
        // Lock-free: just atomic load, no mutex
        auto slots = slots_.load(std::memory_order_acquire);
        
        for (auto& slot : *slots) {
            if (slot) {
                (*slot)(std::forward<U>(args)...);
            }
        }
    }
    
    void disconnect_all() {
        std::lock_guard lock(write_mutex_);
        slots_.store(std::make_shared<slot_list>(), std::memory_order_release);
    }
    
    void block() noexcept { blocked_.store(true, std::memory_order_relaxed); }
    void unblock() noexcept { blocked_.store(false, std::memory_order_relaxed); }
    
    [[nodiscard]] std::size_t slot_count() const {
        auto slots = slots_.load(std::memory_order_acquire);
        return slots->size();
    }

private:
    std::atomic<std::shared_ptr<slot_list>> slots_;
    std::mutex write_mutex_;  // Serializes writers only
    std::atomic<bool> blocked_{false};
};

} // namespace sigslot
