// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once
#include <atomic>
#include <cstddef>
#include <utility>
#include <sigslot/slot-arena.hpp>

namespace sigslot::detail {

/**
 * @brief Base class for dual-counter intrusive reference counting
 * 
 * Objects that inherit from this class can be managed by intrusive_ptr.
 * Uses dual counters (strong + weak) embedded in the object itself,
 * eliminating the need for a separate control block (unlike std::shared_ptr).
 * 
 * Memory layout (16 bytes for counters):
 * - m_strong: strong reference count (controls object lifetime)
 * - m_weak: weak reference count + 1 while strong > 0
 * 
 * Benefits:
 * - Single allocation (object + refcounts together)
 * - Smaller memory footprint (no control block)
 * - Lock-free weak_ptr::lock() via CAS
 * - Cache-friendly (counters adjacent in memory)
 * - Arena-friendly (no cross-thread deallocation issues)
 */
class intrusive_refcount {
    mutable std::atomic<std::size_t> m_strong{0};
    mutable std::atomic<std::size_t> m_weak{1};  // +1 for strong refs existing
    bool m_arena_allocated = false;
    slot_arena* m_arena = nullptr;  // Optional arena for safe reset tracking
    mutable bool m_pending_weak_notified = false;  // Track if we notified arena of pending weak
    
protected:
    intrusive_refcount() noexcept = default;
    // NOLINTNEXTLINE(hicpp-named-parameter,readability-named-parameter)
    intrusive_refcount(const intrusive_refcount& /*unused*/) noexcept : m_strong(0), m_weak(1), m_arena_allocated(false) {}
    // NOLINTNEXTLINE(cert-oop54-cpp) - intentionally ignores source, refcount is not copied
    intrusive_refcount& operator=(const intrusive_refcount& /*unused*/) noexcept { return *this; }
    
    /**
     * @brief Called when strong count reaches zero
     * 
     * Calls the destructor but does NOT free memory.
     * Memory is freed later by destroy_weak() when weak count reaches zero.
     */
    virtual void destroy() const {
        // Call destructor only - memory stays valid for weak refs
        this->~intrusive_refcount();
    }
    
public:
    virtual ~intrusive_refcount() = default;
    
    void add_ref() const noexcept {
        m_strong.fetch_add(1, std::memory_order_relaxed);
    }
    
    void release_ref() const noexcept {
        if (m_strong.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // Last strong reference gone
            // Notify arena if we're transitioning to pending weak state
            bool has_weak_refs = m_weak.load(std::memory_order_acquire) > 1;
            if (m_arena && has_weak_refs) {
                m_arena->add_pending_weak();
                m_pending_weak_notified = true;
            }
            
            // Call destroy() which runs the destructor (but doesn't free memory)
            destroy();
            // After destroy(), vtable is invalid - do NOT call virtual functions!
            // Directly handle weak count decrement and memory deallocation
            if (m_weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                // Last weak reference gone - deallocate memory
                // Can't call virtual destroy_weak() here, use non-virtual path
                do_destroy_weak();
            }
        }
    }
    
    void add_weak_ref() const noexcept {
        m_weak.fetch_add(1, std::memory_order_relaxed);
    }
    
    void release_weak_ref() const noexcept {
        if (m_weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // Last weak reference gone - deallocate memory
            // This path is only taken when strong count was already 0,
            // meaning destroy() was already called. Use non-virtual path.
            do_destroy_weak();
        }
    }
    
    /**
     * @brief Atomically try to acquire a strong reference if object is alive
     * @return true if strong ref acquired, false if object already destroyed
     * 
     * This is the lock-free CAS loop used by intrusive_weak_ptr::lock().
     */
    [[nodiscard]] bool try_add_ref() const noexcept {
        std::size_t count = m_strong.load(std::memory_order_relaxed);
        while (count != 0) {
            if (m_strong.compare_exchange_weak(count, count + 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return true;
            }
            // count is updated by compare_exchange_weak on failure
        }
        return false;
    }
    
    [[nodiscard]] std::size_t use_count() const noexcept {
        return m_strong.load(std::memory_order_relaxed);
    }
    
    [[nodiscard]] std::size_t weak_count() const noexcept {
        // Subtract 1 because we always hold +1 while strong > 0
        std::size_t w = m_weak.load(std::memory_order_relaxed);
        std::size_t s = m_strong.load(std::memory_order_relaxed);
        return s > 0 ? w - 1 : w;
    }
    
    // Mark as arena-allocated with optional arena pointer for safe reset tracking
    void set_arena_allocated(slot_arena* arena = nullptr) noexcept { 
        m_arena_allocated = true; 
        m_arena = arena;
    }
    [[nodiscard]] bool is_arena_allocated() const noexcept { return m_arena_allocated; }
    [[nodiscard]] slot_arena* get_arena() const noexcept { return m_arena; }
    
private:
    // Non-virtual memory deallocation - safe to call after destructor
    void do_destroy_weak() const noexcept {
        if (m_arena && m_pending_weak_notified) {
            // Notify arena that pending weak ref is now cleared
            m_arena->remove_pending_weak();
        }
        if (!m_arena_allocated) {
            ::operator delete(const_cast<intrusive_refcount*>(this));
        }
    }
};

/**
 * @brief Smart pointer for intrusive reference counting
 * 
 * Similar to std::shared_ptr but uses embedded reference count.
 * Compatible with objects that inherit from intrusive_refcount.
 */
template<typename T>
class intrusive_ptr {
    T* ptr_ = nullptr;
    
    void add_ref() noexcept {
        if (ptr_) {
            ptr_->add_ref();
        }
    }
    
    void do_release() noexcept {
        if (ptr_) {
            ptr_->release_ref();
            ptr_ = nullptr;
        }
    }
    
public:
    using element_type = T;
    
    // Constructors
    constexpr intrusive_ptr() noexcept = default;
    constexpr intrusive_ptr(std::nullptr_t) noexcept {}
    
    explicit intrusive_ptr(T* p, bool add_ref = true) noexcept : ptr_(p) {
        if (ptr_ && add_ref) {
            ptr_->add_ref();
        }
    }
    
    // Copy constructor
    intrusive_ptr(const intrusive_ptr& other) noexcept : ptr_(other.ptr_) {
        add_ref();
    }
    
    template<typename U>
    intrusive_ptr(const intrusive_ptr<U>& other) noexcept : ptr_(other.get()) {
        add_ref();
    }
    
    // Move constructor
    intrusive_ptr(intrusive_ptr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    template<typename U>
    intrusive_ptr(intrusive_ptr<U>&& other) noexcept : ptr_(other.release()) {}
    
    // Destructor
    ~intrusive_ptr() {
        do_release();
    }
    
    // Assignment operators
    intrusive_ptr& operator=(const intrusive_ptr& other) noexcept {
        intrusive_ptr(other).swap(*this);
        return *this;
    }
    
    intrusive_ptr& operator=(intrusive_ptr&& other) noexcept {
        intrusive_ptr(std::move(other)).swap(*this);
        return *this;
    }
    
    intrusive_ptr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }
    
    // Observers
    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    
    [[nodiscard]] std::size_t use_count() const noexcept {
        return ptr_ ? ptr_->use_count() : 0;
    }
    
    // Modifiers
    void reset(T* p = nullptr) noexcept {
        intrusive_ptr(p).swap(*this);
    }
    
    T* release() noexcept {
        T* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }
    
    void swap(intrusive_ptr& other) noexcept {
        std::swap(ptr_, other.ptr_);
    }
    
    // Comparison operators
    template<typename U>
    bool operator==(const intrusive_ptr<U>& other) const noexcept {
        return ptr_ == other.get();
    }
    
    template<typename U>
    bool operator!=(const intrusive_ptr<U>& other) const noexcept {
        return ptr_ != other.get();
    }
    
    bool operator==(std::nullptr_t) const noexcept { return ptr_ == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return ptr_ != nullptr; }
};

/**
 * @brief Weak reference for intrusive_ptr with dual-counter support
 * 
 * Provides a non-owning reference that can safely detect if the object
 * is still alive and atomically acquire a strong reference.
 * 
 * Unlike the previous implementation, this properly prevents use-after-free
 * by holding a weak reference count that keeps the counters valid.
 */
template<typename T>
class intrusive_weak_ptr {
    T* ptr_ = nullptr;
    
    void add_weak() noexcept {
        if (ptr_) {
            ptr_->add_weak_ref();
        }
    }
    
    // Note: TSan (GCC) reports a race when release_weak_ref() is called while
    // the pointed-to object's destructor is running on another thread. This is
    // benign because we only access m_weak (atomic with trivial destructor) and
    // memory stays valid until weak count reaches 0. Same semantics as
    // std::weak_ptr control block. Suppressed in test/tsan_suppressions.txt.
    void release_weak() noexcept {
        if (ptr_) {
            ptr_->release_weak_ref();
        }
    }
    
public:
    constexpr intrusive_weak_ptr() noexcept = default;
    
    intrusive_weak_ptr(const intrusive_ptr<T>& strong) noexcept : ptr_(strong.get()) {
        add_weak();
    }
    
    // Allow construction from derived types
    template<typename U>
        requires std::is_base_of_v<T, U>
    intrusive_weak_ptr(const intrusive_ptr<U>& strong) noexcept : ptr_(strong.get()) {
        add_weak();
    }
    
    // Copy operations - must manage weak count
    intrusive_weak_ptr(const intrusive_weak_ptr& other) noexcept : ptr_(other.ptr_) {
        add_weak();
    }
    
    intrusive_weak_ptr& operator=(const intrusive_weak_ptr& other) noexcept {
        if (this != &other) {
            release_weak();
            ptr_ = other.ptr_;
            add_weak();
        }
        return *this;
    }
    
    // Move operations - transfer ownership, no atomic ops needed
    intrusive_weak_ptr(intrusive_weak_ptr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    intrusive_weak_ptr& operator=(intrusive_weak_ptr&& other) noexcept {
        if (this != &other) {
            release_weak();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    ~intrusive_weak_ptr() {
        release_weak();
    }
    
    intrusive_weak_ptr& operator=(const intrusive_ptr<T>& strong) noexcept {
        release_weak();
        ptr_ = strong.get();
        add_weak();
        return *this;
    }
    
    [[nodiscard]] bool expired() const noexcept {
        return !ptr_ || ptr_->use_count() == 0;
    }
    
    /**
     * @brief Atomically acquire a strong reference if object is alive
     * @return intrusive_ptr to object, or empty if expired
     * 
     * This is lock-free: uses CAS loop to safely increment strong count
     * only if it's > 0. No race condition with release_ref().
     */
    [[nodiscard]] intrusive_ptr<T> lock() const noexcept {
        if (!ptr_) {
            return intrusive_ptr<T>();
        }
        // Atomically try to increment strong count if > 0
        if (ptr_->try_add_ref()) {
            // Successfully acquired strong reference
            // Return without adding ref again (already done by try_add_ref)
            return intrusive_ptr<T>(ptr_, false);
        }
        return intrusive_ptr<T>();
    }
    
    void reset() noexcept {
        release_weak();
        ptr_ = nullptr;
    }
    
    void swap(intrusive_weak_ptr& other) noexcept {
        std::swap(ptr_, other.ptr_);
    }
};

/**
 * @brief Helper to create intrusive_ptr
 * 
 * Creates a new object and wraps it in intrusive_ptr.
 * The refcount starts at 0 and is incremented by the intrusive_ptr constructor.
 */
template<typename T, typename... Args>
intrusive_ptr<T> make_intrusive(Args&&... args) {
    return intrusive_ptr<T>(new T(std::forward<Args>(args)...), true);
}

/**
 * @brief Static pointer cast for intrusive_ptr
 */
template<typename T, typename U>
intrusive_ptr<T> static_pointer_cast(const intrusive_ptr<U>& ptr) noexcept {
    return intrusive_ptr<T>(static_cast<T*>(ptr.get()));
}

} // namespace sigslot::detail
