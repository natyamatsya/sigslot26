// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once
#include <atomic>
#include <cstddef>
#include <utility>

namespace sigslot::detail {

/**
 * @brief Base class for intrusive reference counting
 * 
 * Objects that inherit from this class can be managed by intrusive_ptr.
 * The reference count is embedded in the object itself, eliminating the
 * need for a separate control block (unlike std::shared_ptr).
 * 
 * Benefits:
 * - Single allocation (object + refcount together)
 * - Smaller memory footprint (no control block)
 * - Faster copy operations (one atomic op instead of two)
 * - Arena-friendly (no cross-thread deallocation issues)
 */
class intrusive_refcount {
    mutable std::atomic<std::size_t> m_refcount{0};
    bool m_arena_allocated = false;
    
protected:
    intrusive_refcount() noexcept = default;
    intrusive_refcount(const intrusive_refcount&) noexcept : m_refcount(0), m_arena_allocated(false) {}
    intrusive_refcount& operator=(const intrusive_refcount&) noexcept { return *this; }
    
public:
    virtual ~intrusive_refcount() = default;
    
    void add_ref() const noexcept {
        m_refcount.fetch_add(1, std::memory_order_relaxed);
    }
    
    void release_ref() const noexcept {
        if (m_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            if (m_arena_allocated) {
                // Arena allocation: call destructor but don't free memory
                this->~intrusive_refcount();
            } else {
                // Heap allocation: normal delete
                delete this;
            }
        }
    }
    
    [[nodiscard]] std::size_t use_count() const noexcept {
        return m_refcount.load(std::memory_order_relaxed);
    }
    
    // Mark as arena-allocated (called by make_slot_ptr)
    void set_arena_allocated() noexcept { m_arena_allocated = true; }
    [[nodiscard]] bool is_arena_allocated() const noexcept { return m_arena_allocated; }
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
 * @brief Weak reference for intrusive_ptr
 * 
 * Provides a non-owning reference that can detect if the object is still alive.
 * Unlike std::weak_ptr, this doesn't prevent deallocation - it just checks
 * if the refcount is > 0.
 */
template<typename T>
class intrusive_weak_ptr {
    T* ptr_ = nullptr;
    
public:
    constexpr intrusive_weak_ptr() noexcept = default;
    
    intrusive_weak_ptr(const intrusive_ptr<T>& strong) noexcept : ptr_(strong.get()) {}
    
    // Allow construction from derived types
    template<typename U>
        requires std::is_base_of_v<T, U>
    intrusive_weak_ptr(const intrusive_ptr<U>& strong) noexcept : ptr_(strong.get()) {}
    
    // Copy operations
    intrusive_weak_ptr(const intrusive_weak_ptr& other) noexcept : ptr_(other.ptr_) {}
    
    intrusive_weak_ptr& operator=(const intrusive_weak_ptr& other) noexcept {
        ptr_ = other.ptr_;
        return *this;
    }
    
    // Move operations - critical for vector reallocation!
    intrusive_weak_ptr(intrusive_weak_ptr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    intrusive_weak_ptr& operator=(intrusive_weak_ptr&& other) noexcept {
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
        return *this;
    }
    
    intrusive_weak_ptr& operator=(const intrusive_ptr<T>& strong) noexcept {
        ptr_ = strong.get();
        return *this;
    }
    
    [[nodiscard]] bool expired() const noexcept {
        return !ptr_ || ptr_->use_count() == 0;
    }
    
    [[nodiscard]] intrusive_ptr<T> lock() const noexcept {
        if (expired()) {
            return intrusive_ptr<T>();
        }
        return intrusive_ptr<T>(ptr_);
    }
    
    void reset() noexcept {
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
