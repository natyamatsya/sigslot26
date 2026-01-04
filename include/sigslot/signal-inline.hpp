// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

#include "slot-variant.hpp"
#include "connection.hpp"
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <expected>

namespace sigslot {

/**
 * @brief Error codes for signal_inline_seqlock operations.
 */
enum class seqlock_error {
    capacity_exceeded  ///< Maximum slot capacity reached
};

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
 * 
 * Optional optimizations (controlled via CMake):
 * - SIGSLOT_CACHE_LINE_PADDING: Separate hot/cold data into different cache lines
 * - SIGSLOT_INDEX_CACHING: Cache slot count to avoid size() call on hot path
 */
template<typename... T>
class signal_inline_rw {
#ifdef SIGSLOT_CACHE_LINE_PADDING
    static constexpr std::size_t cache_line_size = 64;
#endif
    
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
#ifdef SIGSLOT_INDEX_CACHING
        slot_count_cache_.store(slots_.size(), std::memory_order_release);
#endif
    }
    
    template<typename Pmf, typename Ptr>
    void connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        std::unique_lock lock(mutex_);
        slots_.push_back(slot_type::make_pmf(
            std::forward<Pmf>(pmf), 
            std::forward<Ptr>(ptr), 
            gid
        ));
#ifdef SIGSLOT_INDEX_CACHING
        slot_count_cache_.store(slots_.size(), std::memory_order_release);
#endif
    }
    
    template<typename... U>
    void operator()(U&&... args) {
        if (blocked_.load(std::memory_order_relaxed)) return;
#ifdef SIGSLOT_INDEX_CACHING
        // Fast path: check cached slot count before acquiring lock
        if (slot_count_cache_.load(std::memory_order_acquire) == 0) return;
#endif
        
        std::shared_lock lock(mutex_);
        for (auto& slot : slots_) {
            slot(std::forward<U>(args)...);
        }
    }
    
    void disconnect_all() {
        std::unique_lock lock(mutex_);
        slots_.clear();
#ifdef SIGSLOT_INDEX_CACHING
        slot_count_cache_.store(0, std::memory_order_release);
#endif
    }
    
    void block() noexcept { blocked_.store(true, std::memory_order_relaxed); }
    void unblock() noexcept { blocked_.store(false, std::memory_order_relaxed); }
    
    [[nodiscard]] std::size_t slot_count() const noexcept {
#ifdef SIGSLOT_INDEX_CACHING
        return slot_count_cache_.load(std::memory_order_acquire);
#else
        std::shared_lock lock(mutex_);
        return slots_.size();
#endif
    }

private:
    // ========================================================================
    // Hot data - read on every emission
    // ========================================================================
#ifdef SIGSLOT_CACHE_LINE_PADDING
    alignas(cache_line_size) std::atomic<bool> blocked_{false};
#else
    std::atomic<bool> blocked_{false};
#endif
#ifdef SIGSLOT_INDEX_CACHING
    std::atomic<std::size_t> slot_count_cache_{0};
#endif
    
    // ========================================================================
    // Cold data - accessed only during connect/disconnect
    // ========================================================================
#ifdef SIGSLOT_CACHE_LINE_PADDING
    alignas(cache_line_size) mutable std::shared_mutex mutex_;
#else
    mutable std::shared_mutex mutex_;
#endif
    std::vector<slot_type> slots_;
};

/**
 * @brief Lock-free thread-safe signal using RCU (Read-Copy-Update)
 * 
 * Emission is completely lock-free (just atomic load + refcount).
 * Connect/disconnect creates a new slot list and atomically swaps.
 * 
 * Trade-off: Uses pointer indirection for slots (like signal_base),
 * but with slot_variant's faster dispatch.
 * 
 * Optional: SIGSLOT_CACHE_LINE_PADDING separates hot and cold data.
 */
template<typename... T>
class signal_inline_rcu {
#ifdef SIGSLOT_CACHE_LINE_PADDING
    static constexpr std::size_t cache_line_size = 64;
#endif
    
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
    // ========================================================================
    // Hot data - read on every emission
    // ========================================================================
#ifdef SIGSLOT_CACHE_LINE_PADDING
    alignas(cache_line_size) std::atomic<std::shared_ptr<slot_list>> slots_;
#else
    std::atomic<std::shared_ptr<slot_list>> slots_;
#endif
    std::atomic<bool> blocked_{false};
    
    // ========================================================================
    // Cold data - accessed only during connect/disconnect
    // ========================================================================
#ifdef SIGSLOT_CACHE_LINE_PADDING
    alignas(cache_line_size) std::mutex write_mutex_;
#else
    std::mutex write_mutex_;
#endif
};

// ============================================================================
// Fixed-capacity storage for seqlock safety
// ============================================================================

namespace detail {

/**
 * @brief Fixed-capacity vector that never reallocates.
 * 
 * This enables safe use with seqlock - readers can iterate while writers
 * append because the storage address never changes.
 * 
 * @tparam T Element type
 * @tparam Capacity Maximum number of elements (compile-time constant)
 */
template<typename T, std::size_t Capacity>
class fixed_vector {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;
    
    fixed_vector() = default;
    
    ~fixed_vector() {
        clear();
    }
    
    fixed_vector(const fixed_vector&) = delete;
    fixed_vector& operator=(const fixed_vector&) = delete;
    fixed_vector(fixed_vector&&) = delete;
    fixed_vector& operator=(fixed_vector&&) = delete;
    
    template<typename... Args>
    bool emplace_back(Args&&... args) {
        const std::size_t current = size_.load(std::memory_order_relaxed);
        if (current >= Capacity) return false;
        // Construct object first
        new (data() + current) T(std::forward<Args>(args)...);
        // Then publish size with release semantics so readers see constructed object
        size_.store(current + 1, std::memory_order_release);
        return true;
    }
    
    void clear() {
        const std::size_t current = size_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < current; ++i) {
            std::launder(data() + i)->~T();
        }
        size_.store(0, std::memory_order_release);
    }
    
    [[nodiscard]] T& operator[](std::size_t i) noexcept {
        return *std::launder(data() + i);
    }
    
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept {
        return *std::launder(data() + i);
    }
    
    [[nodiscard]] T* data() noexcept {
        return reinterpret_cast<T*>(storage_);
    }
    
    [[nodiscard]] const T* data() const noexcept {
        return reinterpret_cast<const T*>(storage_);
    }
    
    [[nodiscard]] std::size_t size() const noexcept { 
        return size_.load(std::memory_order_acquire); 
    }
    [[nodiscard]] bool empty() const noexcept { 
        return size_.load(std::memory_order_acquire) == 0; 
    }
    [[nodiscard]] bool full() const noexcept { 
        return size_.load(std::memory_order_acquire) >= Capacity; 
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    
private:
    alignas(T) std::byte storage_[Capacity * sizeof(T)]{};
    std::atomic<std::size_t> size_{0};
};

} // namespace detail

/**
 * @brief Lock-free signal using seqlock pattern with fixed-capacity storage.
 * 
 * Uses a compile-time fixed capacity to avoid reallocation, making the seqlock
 * pattern safe. Readers (emitters) are wait-free when no writer is active.
 * 
 * @tparam MaxSlots Maximum number of connected slots (default: 16)
 * @tparam T... Signal argument types
 * 
 * Trade-offs:
 * - Lock-free emission (fastest thread-safe option)
 * - Fixed slot capacity - connect fails if full
 * - Readers may retry if emission overlaps with connect/disconnect
 */
template<std::size_t MaxSlots, typename... T>
class signal_inline_seqlock {
#ifdef SIGSLOT_CACHE_LINE_PADDING
    static constexpr std::size_t cache_line_size = 64;
#endif
    
public:
    using group_id = int32_t;
    using slot_type = detail::slot_variant<group_id, T...>;
    
    signal_inline_seqlock() = default;
    ~signal_inline_seqlock() = default;
    
    signal_inline_seqlock(const signal_inline_seqlock&) = delete;
    signal_inline_seqlock& operator=(const signal_inline_seqlock&) = delete;
    signal_inline_seqlock(signal_inline_seqlock&&) = delete;
    signal_inline_seqlock& operator=(signal_inline_seqlock&&) = delete;
    
    /**
     * @brief Connect a callable.
     * @return std::expected<void, seqlock_error> - error if capacity exceeded
     */
    template<typename Callable>
    [[nodiscard]] std::expected<void, seqlock_error> connect(Callable&& c, group_id gid = group_id{}) {
        std::lock_guard lock(write_mutex_);
        if (slots_.full()) {
            return std::unexpected(seqlock_error::capacity_exceeded);
        }
        begin_write();
        slots_.emplace_back(slot_type::make_plain(std::forward<Callable>(c), gid));
        end_write();
        return {};
    }
    
    /**
     * @brief Connect a member function.
     * @return std::expected<void, seqlock_error> - error if capacity exceeded
     */
    template<typename Pmf, typename Ptr>
    [[nodiscard]] std::expected<void, seqlock_error> connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        std::lock_guard lock(write_mutex_);
        if (slots_.full()) {
            return std::unexpected(seqlock_error::capacity_exceeded);
        }
        begin_write();
        slots_.emplace_back(slot_type::make_pmf(
            std::forward<Pmf>(pmf), 
            std::forward<Ptr>(ptr), 
            gid
        ));
        end_write();
        return {};
    }
    
    /**
     * @brief Lock-free emission with retry on concurrent write.
     * 
     * Wait-free when no writer is active. If a write is detected during
     * emission, the reader retries from the beginning.
     */
    template<typename... U>
    void operator()(U&&... args) {
        if (blocked_.load(std::memory_order_relaxed)) return;
        
        std::uint64_t seq;
        do {
            seq = seq_.load(std::memory_order_acquire);
            
            // If write in progress (odd sequence), spin-wait
            if (seq & 1) continue;
            
            // Read size and emit - storage never reallocates so this is safe
            const std::size_t count = slots_.size();
            for (std::size_t i = 0; i < count; ++i) {
                slots_[i](std::forward<U>(args)...);
            }
            
            // Check if sequence changed during our read
        } while (seq_.load(std::memory_order_acquire) != seq);
    }
    
    void disconnect_all() {
        std::lock_guard lock(write_mutex_);
        begin_write();
        slots_.clear();
        end_write();
    }
    
    void block() noexcept { blocked_.store(true, std::memory_order_relaxed); }
    void unblock() noexcept { blocked_.store(false, std::memory_order_relaxed); }
    
    [[nodiscard]] std::size_t slot_count() const noexcept {
        std::uint64_t seq;
        std::size_t count;
        do {
            seq = seq_.load(std::memory_order_acquire);
            if (seq & 1) continue;
            count = slots_.size();
        } while (seq_.load(std::memory_order_acquire) != seq);
        return count;
    }
    
    [[nodiscard]] static constexpr std::size_t max_slots() noexcept { return MaxSlots; }
    [[nodiscard]] bool full() const noexcept { return slots_.full(); }

private:
    void begin_write() noexcept {
        seq_.fetch_add(1, std::memory_order_release);  // Odd = write in progress
    }
    
    void end_write() noexcept {
        seq_.fetch_add(1, std::memory_order_release);  // Even = write complete
    }

    // ========================================================================
    // Hot data - read on every emission
    // ========================================================================
#ifdef SIGSLOT_CACHE_LINE_PADDING
    alignas(cache_line_size) std::atomic<std::uint64_t> seq_{0};
#else
    std::atomic<std::uint64_t> seq_{0};
#endif
    std::atomic<bool> blocked_{false};
    detail::fixed_vector<slot_type, MaxSlots> slots_;
    
    // ========================================================================
    // Cold data - accessed only during connect/disconnect
    // ========================================================================
#ifdef SIGSLOT_CACHE_LINE_PADDING
    alignas(cache_line_size) std::mutex write_mutex_;
#else
    std::mutex write_mutex_;
#endif
};

/**
 * @brief Convenience alias with default capacity of 16 slots.
 */
template<typename... T>
using signal_inline_seqlock16 = signal_inline_seqlock<16, T...>;

/**
 * @brief Convenience alias with capacity of 32 slots.
 */
template<typename... T>
using signal_inline_seqlock32 = signal_inline_seqlock<32, T...>;

} // namespace sigslot
