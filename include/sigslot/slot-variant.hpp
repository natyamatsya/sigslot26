// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace sigslot::detail {

/**
 * @brief Tag enumeration for slot variant types.
 * 
 * Ordered by expected frequency of use for branch prediction optimization.
 * The compiler can generate efficient jump tables or branch sequences.
 */
enum class slot_tag : std::uint8_t {
    plain = 0,        // slot<G, F, Args...>           - most common (lambdas, free functions)
    pmf,              // slot_pmf<G, Pmf, Ptr, Args...> - member function with raw pointer
    tracked,          // slot_tracked<G, F, WeakPtr, Args...> - callable with lifetime tracking
    pmf_tracked,      // slot_pmf_tracked<G, Pmf, WeakPtr, Args...> - PMF with lifetime tracking
    extended,         // slot_extended<G, F, Args...> - callable with connection argument
    pmf_extended,     // slot_pmf_extended<G, Pmf, Ptr, Args...> - PMF with connection argument
    empty             // No slot stored (default constructed or moved-from)
};

// Forward declaration
template<typename Group, typename... Args>
class slot_variant;

/**
 * @brief Storage traits for different slot types within slot_variant.
 * 
 * Each slot type needs:
 * - Storage layout (what data to store)
 * - Call function (how to invoke the callable)
 * - Connected check (for tracked slots)
 * - Destruction logic
 */

// Plain slot storage: just the callable
template<typename Func>
struct plain_slot_storage {
    using func_type = std::decay_t<Func>;
    func_type func;
    
    template<typename F>
    explicit plain_slot_storage(F&& f) : func(std::forward<F>(f)) {}
    
    template<typename... Args>
    void call(Args&&... args) {
        func(std::forward<Args>(args)...);
    }
    
    static constexpr bool is_tracked = false;
};

// PMF slot storage: pointer-to-member-function + object pointer
template<typename Pmf, typename Ptr>
struct pmf_slot_storage {
    using pmf_type = std::decay_t<Pmf>;
    using ptr_type = std::decay_t<Ptr>;
    pmf_type pmf;
    ptr_type ptr;
    
    template<typename F, typename P>
    pmf_slot_storage(F&& f, P&& p) : pmf(std::forward<F>(f)), ptr(std::forward<P>(p)) {}
    
    template<typename... Args>
    void call(Args&&... args) {
        ((*ptr).*pmf)(std::forward<Args>(args)...);
    }
    
    static constexpr bool is_tracked = false;
};

// Tracked slot storage: callable + weak_ptr for lifetime tracking
template<typename Func, typename WeakPtr>
struct tracked_slot_storage {
    using func_type = std::decay_t<Func>;
    using weak_type = std::decay_t<WeakPtr>;
    func_type func;
    weak_type ptr;
    
    template<typename F, typename P>
    tracked_slot_storage(F&& f, P&& p) : func(std::forward<F>(f)), ptr(std::forward<P>(p)) {}
    
    template<typename... Args>
    bool call_if_valid(Args&&... args) {
        auto sp = ptr.lock();
        if (!sp) return false;
        func(std::forward<Args>(args)...);
        return true;
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        return !ptr.expired();
    }
    
    static constexpr bool is_tracked = true;
};

// PMF tracked slot storage: PMF + weak_ptr
template<typename Pmf, typename WeakPtr>
struct pmf_tracked_slot_storage {
    using pmf_type = std::decay_t<Pmf>;
    using weak_type = std::decay_t<WeakPtr>;
    pmf_type pmf;
    weak_type ptr;
    
    template<typename F, typename P>
    pmf_tracked_slot_storage(F&& f, P&& p) : pmf(std::forward<F>(f)), ptr(std::forward<P>(p)) {}
    
    template<typename... Args>
    bool call_if_valid(Args&&... args) {
        auto sp = ptr.lock();
        if (!sp) return false;
        ((*sp).*pmf)(std::forward<Args>(args)...);
        return true;
    }
    
    [[nodiscard]] bool is_valid() const noexcept {
        return !ptr.expired();
    }
    
    static constexpr bool is_tracked = true;
};

// connection is imported via using declaration from sigslot namespace in signal.hpp

// Extended slot storage: callable that receives connection as first arg
template<typename Func, typename Connection>
struct extended_slot_storage {
    using func_type = std::decay_t<Func>;
    func_type func;
    Connection conn;
    
    template<typename F>
    explicit extended_slot_storage(F&& f) : func(std::forward<F>(f)), conn() {}
    
    void set_connection(Connection c) { conn = std::move(c); }
    
    template<typename... Args>
    void call(Args&&... args) {
        func(conn, std::forward<Args>(args)...);
    }
    
    static constexpr bool is_tracked = false;
};

// PMF extended slot storage
template<typename Pmf, typename Ptr, typename Connection>
struct pmf_extended_slot_storage {
    using pmf_type = std::decay_t<Pmf>;
    using ptr_type = std::decay_t<Ptr>;
    pmf_type pmf;
    ptr_type ptr;
    Connection conn;
    
    template<typename F, typename P>
    pmf_extended_slot_storage(F&& f, P&& p) : pmf(std::forward<F>(f)), ptr(std::forward<P>(p)), conn() {}
    
    void set_connection(Connection c) { conn = std::move(c); }
    
    template<typename... Args>
    void call(Args&&... args) {
        ((*ptr).*pmf)(conn, std::forward<Args>(args)...);
    }
    
    static constexpr bool is_tracked = false;
};

/**
 * @brief Variant-based slot implementation eliminating virtual dispatch.
 * 
 * Instead of inheritance + vtable, we use:
 * - Inline function pointer for the call operation (hot path)
 * - uint8_t tag for type discrimination (cold path operations)
 * - Manual union storage for slot data
 * 
 * This eliminates one level of indirection on every emission:
 * - Old: load vtable ptr → load fn ptr from vtable → indirect call
 * - New: load fn ptr from slot → indirect call
 * 
 * @tparam Group The group ID type for slot ordering
 * @tparam Args The signal argument types
 */
template<typename Group, typename... Args>
class slot_variant {
public:
    using group_type = Group;
    
    // Function pointer type for the call operation
    // Returns bool: true if call succeeded, false if slot expired (tracked only)
    using call_fn_t = bool(*)(void* storage, Args...);
    
    // Function pointer for destruction
    using destroy_fn_t = void(*)(void* storage);
    
    // Storage size: 64 bytes covers most callables including std::function
    // This is a tunable parameter - can be adjusted based on profiling
    static constexpr std::size_t storage_size = 64;
    static constexpr std::size_t storage_align = alignof(std::max_align_t);

private:
    // ========================================================================
    // Hot data - accessed on every emission (packed into first cache line)
    // ========================================================================
    call_fn_t call_ = nullptr;                      // 8 bytes - inline fn ptr
    alignas(storage_align) 
        std::byte storage_[storage_size] = {};      // 64 bytes - variant storage
    std::atomic<bool> connected_{false};            // 1 byte
    std::atomic<bool> blocked_{false};              // 1 byte
    slot_tag tag_ = slot_tag::empty;                // 1 byte
    
    // ========================================================================
    // Cold data - accessed on connect/disconnect only
    // ========================================================================
    destroy_fn_t destroy_ = nullptr;                // 8 bytes
    std::atomic<std::size_t> index_{0};             // 8 bytes
    Group group_{};                                 // 4 bytes (typically int32_t)
    
public:
    // ========================================================================
    // Constructors
    // ========================================================================
    
    slot_variant() = default;
    
    ~slot_variant() {
        destroy_storage();
    }
    
    // Non-copyable (slots have identity via index)
    slot_variant(const slot_variant&) = delete;
    slot_variant& operator=(const slot_variant&) = delete;
    
    // Movable
    slot_variant(slot_variant&& other) noexcept
        : call_(other.call_)
        , connected_(other.connected_.load(std::memory_order_relaxed))
        , blocked_(other.blocked_.load(std::memory_order_relaxed))
        , tag_(other.tag_)
        , destroy_(other.destroy_)
        , index_(other.index_.load(std::memory_order_relaxed))
        , group_(other.group_)
    {
        // Move storage bytes
        std::memcpy(storage_, other.storage_, storage_size);
        
        // Clear other
        other.call_ = nullptr;
        other.destroy_ = nullptr;
        other.tag_ = slot_tag::empty;
        other.connected_.store(false, std::memory_order_relaxed);
    }
    
    slot_variant& operator=(slot_variant&& other) noexcept {
        if (this != &other) {
            destroy_storage();
            
            call_ = other.call_;
            std::memcpy(storage_, other.storage_, storage_size);
            connected_.store(other.connected_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            blocked_.store(other.blocked_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            tag_ = other.tag_;
            destroy_ = other.destroy_;
            index_.store(other.index_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            group_ = other.group_;
            
            other.call_ = nullptr;
            other.destroy_ = nullptr;
            other.tag_ = slot_tag::empty;
            other.connected_.store(false, std::memory_order_relaxed);
        }
        return *this;
    }
    
    // ========================================================================
    // Emplacement constructors for each slot type
    // ========================================================================
    
    /**
     * @brief Emplace a plain callable slot (lambda, free function, functor)
     */
    template<typename Func>
    static slot_variant make_plain(Func&& f, Group gid) {
        using storage_t = plain_slot_storage<Func>;
        static_assert(sizeof(storage_t) <= storage_size, 
            "Callable too large for slot_variant storage. Consider increasing storage_size.");
        
        slot_variant slot;
        slot.tag_ = slot_tag::plain;
        slot.group_ = gid;
        slot.connected_.store(true, std::memory_order_relaxed);
        
        // Construct storage in-place
        new (slot.storage_) storage_t(std::forward<Func>(f));
        
        // Set up function pointers (std::launder required after placement new)
        slot.call_ = [](void* s, Args... args) -> bool {
            std::launder(static_cast<storage_t*>(s))->call(std::forward<Args>(args)...);
            return true;
        };
        slot.destroy_ = [](void* s) {
            std::launder(static_cast<storage_t*>(s))->~storage_t();
        };
        
        return slot;
    }
    
    /**
     * @brief Emplace a pointer-to-member-function slot
     */
    template<typename Pmf, typename Ptr>
    static slot_variant make_pmf(Pmf&& pmf, Ptr&& ptr, Group gid) {
        using storage_t = pmf_slot_storage<Pmf, Ptr>;
        static_assert(sizeof(storage_t) <= storage_size,
            "PMF slot too large for slot_variant storage.");
        
        slot_variant slot;
        slot.tag_ = slot_tag::pmf;
        slot.group_ = gid;
        slot.connected_.store(true, std::memory_order_relaxed);
        
        new (slot.storage_) storage_t(std::forward<Pmf>(pmf), std::forward<Ptr>(ptr));
        
        // std::launder required after placement new
        slot.call_ = [](void* s, Args... args) -> bool {
            std::launder(static_cast<storage_t*>(s))->call(std::forward<Args>(args)...);
            return true;
        };
        slot.destroy_ = [](void* s) {
            std::launder(static_cast<storage_t*>(s))->~storage_t();
        };
        
        return slot;
    }
    
    /**
     * @brief Emplace a tracked callable slot (with weak_ptr lifetime management)
     */
    template<typename Func, typename WeakPtr>
    static slot_variant make_tracked(Func&& f, WeakPtr&& ptr, Group gid) {
        using storage_t = tracked_slot_storage<Func, WeakPtr>;
        static_assert(sizeof(storage_t) <= storage_size,
            "Tracked slot too large for slot_variant storage.");
        
        slot_variant slot;
        slot.tag_ = slot_tag::tracked;
        slot.group_ = gid;
        slot.connected_.store(true, std::memory_order_relaxed);
        
        new (slot.storage_) storage_t(std::forward<Func>(f), std::forward<WeakPtr>(ptr));
        
        // std::launder required after placement new
        slot.call_ = [](void* s, Args... args) -> bool {
            return std::launder(static_cast<storage_t*>(s))->call_if_valid(std::forward<Args>(args)...);
        };
        slot.destroy_ = [](void* s) {
            std::launder(static_cast<storage_t*>(s))->~storage_t();
        };
        
        return slot;
    }
    
    /**
     * @brief Emplace a tracked PMF slot
     */
    template<typename Pmf, typename WeakPtr>
    static slot_variant make_pmf_tracked(Pmf&& pmf, WeakPtr&& ptr, Group gid) {
        using storage_t = pmf_tracked_slot_storage<Pmf, WeakPtr>;
        static_assert(sizeof(storage_t) <= storage_size,
            "PMF tracked slot too large for slot_variant storage.");
        
        slot_variant slot;
        slot.tag_ = slot_tag::pmf_tracked;
        slot.group_ = gid;
        slot.connected_.store(true, std::memory_order_relaxed);
        
        new (slot.storage_) storage_t(std::forward<Pmf>(pmf), std::forward<WeakPtr>(ptr));
        
        // std::launder required after placement new
        slot.call_ = [](void* s, Args... args) -> bool {
            return std::launder(static_cast<storage_t*>(s))->call_if_valid(std::forward<Args>(args)...);
        };
        slot.destroy_ = [](void* s) {
            std::launder(static_cast<storage_t*>(s))->~storage_t();
        };
        
        return slot;
    }
    
    // ========================================================================
    // Hot path: Call operator (optimized for emission)
    // ========================================================================
    
    /**
     * @brief Invoke the slot with arguments.
     * 
     * This is the hot path - called on every signal emission.
     * Uses inline function pointer to avoid vtable indirection.
     * 
     * @return true if slot was called, false if blocked/disconnected/expired
     */
    template<typename... U>
    bool operator()(U&&... args) {
        // Fast path: check connected and blocked flags (relaxed - advisory only)
        if (!connected_.load(std::memory_order_relaxed)) {
            return false;
        }
        if (blocked_.load(std::memory_order_relaxed)) {
            return false;
        }
        
        // Call through inline function pointer (single indirection)
        return call_(storage_, std::forward<U>(args)...);
    }
    
    // ========================================================================
    // State accessors
    // ========================================================================
    
    [[nodiscard]] bool connected() const noexcept {
        // For tracked slots, also check if the tracked object is still alive
        if (tag_ == slot_tag::tracked || tag_ == slot_tag::pmf_tracked) {
            return connected_.load(std::memory_order_relaxed) && is_tracking_valid();
        }
        return connected_.load(std::memory_order_relaxed);
    }
    
    bool disconnect() noexcept {
        return connected_.exchange(false, std::memory_order_acq_rel);
    }
    
    [[nodiscard]] bool blocked() const noexcept {
        return blocked_.load(std::memory_order_relaxed);
    }
    
    void block() noexcept {
        blocked_.store(true, std::memory_order_relaxed);
    }
    
    void unblock() noexcept {
        blocked_.store(false, std::memory_order_relaxed);
    }
    
    [[nodiscard]] slot_tag tag() const noexcept { return tag_; }
    [[nodiscard]] Group const& group() const noexcept { return group_; }
    
    [[nodiscard]] std::size_t index() const noexcept {
        return index_.load(std::memory_order_relaxed);
    }
    
    void set_index(std::size_t idx) noexcept {
        index_.store(idx, std::memory_order_relaxed);
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return tag_ == slot_tag::empty;
    }
    
private:
    void destroy_storage() noexcept {
        if (destroy_ && tag_ != slot_tag::empty) {
            destroy_(storage_);
            destroy_ = nullptr;
            call_ = nullptr;
            tag_ = slot_tag::empty;
        }
    }
    
    /**
     * @brief Check if tracked object is still valid (cold path)
     */
    [[nodiscard]] bool is_tracking_valid() const noexcept {
        switch (tag_) {
            case slot_tag::tracked: {
                // We need to access the weak_ptr to check validity
                // This requires knowing the exact storage type, which we don't at runtime
                // For now, return true and let the call fail
                // TODO: Store a validity check function pointer for tracked slots
                return true;
            }
            case slot_tag::pmf_tracked: {
                return true;
            }
            default:
                return true;
        }
    }
};

/**
 * @brief Type trait to check if a type fits in slot_variant storage
 */
template<typename T, typename Group, typename... Args>
inline constexpr bool fits_in_slot_variant_v = 
    sizeof(T) <= slot_variant<Group, Args...>::storage_size;

} // namespace sigslot::detail
