// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: palacaze/sigslot contributors
// SPDX-FileCopyrightText: mousebyte/sigslot20 contributors
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <thread>
#include <vector>
#include <optional>
#include <concepts>

// Optional: Enable slot memory pool for faster connect performance
// Define SIGSLOT_USE_SLOT_POOL to enable thread-local memory pooling
// Options: ARENA (fastest), PMR (portable), or undefined (default allocator)
#ifdef SIGSLOT_USE_SLOT_POOL
    #if SIGSLOT_USE_SLOT_POOL == 2
        #include "slot-arena.hpp"  // Custom arena allocator
    #else
        #include <memory_resource>  // std::pmr
    #endif
#endif

// Optional: Use intrusive reference counting instead of shared_ptr
// Enables arena allocation without cross-thread issues
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
    #include "intrusive-ptr.hpp"
#endif

#include "signal-sbo.hpp"

// Optional: Use variant-based slot storage for eliminating virtual dispatch
#ifdef SIGSLOT_USE_SLOT_VARIANT
    #include "slot-variant.hpp"
#endif

// Cache line size for preventing false sharing between threads.
// When multiple threads access adjacent memory locations, they may experience
// "false sharing" - cache invalidation even though they access different variables.
// Aligning hot atomic variables to cache line boundaries prevents this.
// Using 64 bytes: standard cache line size on x86-64, ARM64, and most modern platforms.
// We avoid std::hardware_destructive_interference_size due to GCC's -Winterference-size
// warning about ABI instability, and the value is 64 on all major platforms anyway.
inline constexpr std::size_t sigslot_cache_line_size = 64;

#if defined __clang__ || (__GNUC__ > 5)
#define SIGSLOT_MAY_ALIAS __attribute__((__may_alias__))
#else
#define SIGSLOT_MAY_ALIAS
#endif

namespace sigslot {

// Forward declaration needed for slot_extended classes
class connection;

namespace detail {

// Used to detect an object of observer type
struct observer_type {};

} // namespace detail

namespace trait {

/**
 * @brief Pointers that can be converted to a weak pointer concept for tracking
 * purpose must implement the to_weak() function in order to make use of
 * ADL to convert that type and make it usable
 */

template<typename T>
std::weak_ptr<T> to_weak(std::weak_ptr<T> w) {
    return w;
}

template<typename T>
std::weak_ptr<T> to_weak(std::shared_ptr<T> s) {
    return s;
}

template<typename F, typename... T>
concept Callable = requires(F f, T... ts) { f(ts...); };

template<typename F, typename P, typename... T>
concept MemberCallable = requires(F f, P p, T... ts) { ((*p).*f)(ts...); };

template<typename T>
concept WeakPtr = requires(T p) {
    p.expired();
    p.lock();
    p.reset();
};

template<typename T>
concept WeakPtrCompatible = requires(T t) {
    { to_weak(t) } -> WeakPtr;
};

template<typename T>
concept Functor =
    std::is_member_function_pointer_v<decltype(&std::remove_reference_t<T>::operator())>;

template<typename T>
concept Pointer = std::is_pointer_v<T>;

template<typename T>
concept Function = std::is_function_v<T>;

template<typename T>
concept MemFnPointer = std::is_member_function_pointer_v<T>;

template<typename T>
concept Observer = std::is_base_of_v<::sigslot::detail::observer_type, std::remove_pointer_t<T>>;

} // namespace trait

template<typename T>
concept GroupId = requires(T g1, T g2) {
    requires std::is_default_constructible_v<T>;
    requires std::is_copy_constructible_v<T>;
    { g1 < g2 } -> std::same_as<bool>;
    { g1 == g2 } -> std::same_as<bool>;
};

template<GroupId, typename, typename...>
class signal_base;

namespace detail {

/**
 * @brief The following function_traits and object_pointer series of templates are
 * used to circumvent the type-erasing that takes place in the slot_base
 * implementations. They are used to compare the stored functions and objects
 * with another one for disconnection purpose.
 */

/**
 * @brief Function pointers and member function pointers size differ from compiler to
 * compiler, and for virtual members compared to non virtual members. On some
 * compilers, multiple inheritance has an impact too. Hence, we form a union
 * big enough to store any kind of function pointer.
 * 
 * The mock namespace defines classes with multiple inheritance to ensure func_ptr
 * is large enough to store PMFs from any class hierarchy. This enables portable
 * PMF comparison via byte equality without relying on RTTI.
 */
namespace mock {

struct a {
    virtual ~a() = default;
    void f();
    virtual void g();
};
struct b {
    virtual ~b() = default;
    virtual void h();
};
struct c : a, b {
    void g() override;
};

union fun_types {
    decltype(&c::g) m;
    decltype(&a::g) v;
    decltype(&a::f) d;
    void (*f)();
    void* o;
};

} // namespace mock

/**
 * @brief This union is used to compare function pointers
 * Generic callables cannot be compared. Here we compare pointers but there is
 * no guarantee that this always works.
 */
union SIGSLOT_MAY_ALIAS func_ptr {
    void* value() { return &data[0]; }

    [[nodiscard]] const void* value() const { return &data[0]; }

    template<typename T>
    T& value() {
        return *static_cast<T*>(value());
    }

    template<typename T>
    [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] const T& value() const {
        return *static_cast<const T*>(value());
    }

    inline explicit operator bool() const { return value() != nullptr; }

    inline bool operator==(const func_ptr& o) const {
        return std::equal(std::begin(data), std::end(data), std::begin(o.data));
    }

    mock::fun_types _;
    // NOLINTNEXTLINE(hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
    char data[sizeof(mock::fun_types)]; // TODO(CK) use std::array<> instead!
};

template<typename T>
struct function_traits {
    static void ptr(const T& /*t*/, func_ptr& d) { d.value<std::nullptr_t>() = nullptr; }

    static constexpr bool is_disconnectable = false;
    static constexpr bool must_check_object = true;
};

template<trait::Function T>
struct function_traits<T> {
    static void ptr(T& t, func_ptr& d) { d.value<T*>() = &t; }

    static constexpr bool is_disconnectable = true;
    static constexpr bool must_check_object = false;
};

template<trait::Function T>
struct function_traits<T*> {
    static void ptr(T* t, func_ptr& d) { d.value<T*>() = t; }

    static constexpr bool is_disconnectable = true;
    static constexpr bool must_check_object = false;
};

/**
 * @brief Traits for pointer-to-member-function (PMF) types.
 * 
 * PMF disconnection works without RTTI by comparing the raw bytes of the PMF value.
 * This is portable across all compilers (MSVC, GCC, Clang, clang-cl) because:
 * - PMFs encode class-specific information (vtable offsets, thunks for multiple inheritance)
 * - Different classes produce different binary representations even for same-named methods
 * - The func_ptr union is sized to accommodate the largest PMF representation (multiple inheritance)
 * 
 * This approach avoids the unreliable typeid() comparison for PMF types, which fails
 * on some compilers (notably clang-cl with multiple inheritance).
 */
template<trait::MemFnPointer T>
struct function_traits<T> {
    static void ptr(const T& t, func_ptr& d) { d.value<T>() = t; }

    static constexpr bool is_disconnectable = true;
    static constexpr bool must_check_object = false;
};

// for function objects, the assumption is that we are looking for the call operator
template<trait::Functor T>
struct function_traits<T> {
    using call_type = decltype(&std::remove_reference<T>::type::operator());

    static void ptr(const T& /*t*/, func_ptr& d) {
        function_traits<call_type>::ptr(&T::operator(), d);
    }

    static constexpr bool is_disconnectable = function_traits<call_type>::is_disconnectable;
    static constexpr bool must_check_object = function_traits<call_type>::must_check_object;
};

template<typename T>
func_ptr get_function_ptr(const T& t) {
    func_ptr d{};
    std::uninitialized_fill(std::begin(d.data), std::end(d.data), '\0');
    function_traits<std::decay_t<T>>::ptr(t, d);
    return d;
}

/**
 * @brief obj_ptr is used to store a pointer to an object.
 * The object_pointer traits are needed to handle trackable objects correctly,
 * as they are likely to not be pointers.
 */
using obj_ptr = const void*;

template<typename T>
obj_ptr get_object_ptr(const T& t);

template<typename T>
struct object_pointer {
    static obj_ptr get(const T& /*unused*/) { return nullptr; }
};

template<typename T>
    requires trait::Pointer<T*>
struct object_pointer<T*> {
    static obj_ptr get(const T* t) { return reinterpret_cast<obj_ptr>(t); }
};

template<trait::WeakPtr T>
struct object_pointer<T> {
    static obj_ptr get(const T& t) {
        auto p = t.lock();
        return get_object_ptr(p);
    }
};

template<trait::WeakPtrCompatible T>
    requires(!trait::Pointer<T> && !trait::WeakPtr<T>)
struct object_pointer<T> {
    static obj_ptr get(const T& t) { return t ? reinterpret_cast<obj_ptr>(t.get()) : nullptr; }
};

template<typename T>
obj_ptr get_object_ptr(const T& t) {
    return object_pointer<T>::get(t);
}

// noop mutex for thread-unsafe use
struct null_mutex {
    null_mutex() noexcept = default;
    ~null_mutex() noexcept = default;
    null_mutex(const null_mutex&) = delete;
    null_mutex& operator=(const null_mutex&) = delete;
    null_mutex(null_mutex&&) = delete;
    null_mutex& operator=(null_mutex&&) = delete;

    inline static bool try_lock() noexcept { return true; }
    inline void lock() noexcept {}
    inline void unlock() noexcept {}
};

/**
 * @brief A spin mutex that yields, mostly for use in benchmarks and scenarii that invoke
 * slots at a very high pace.
 * One should almost always prefer a standard mutex over this.
 */
struct spin_mutex {
    spin_mutex() noexcept = default;
    ~spin_mutex() noexcept = default;
    spin_mutex(spin_mutex const&) = delete;
    spin_mutex& operator=(const spin_mutex&) = delete;
    spin_mutex(spin_mutex&&) = delete;
    spin_mutex& operator=(spin_mutex&&) = delete;

    void lock() noexcept {
        while (true) {
            while (!state.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            if (try_lock()) {
                break;
            }
        }
    }

    bool try_lock() noexcept { return state.exchange(false, std::memory_order_acquire); }

    void unlock() noexcept { state.store(true, std::memory_order_release); }

private:
    std::atomic<bool> state{true};
};

/**
 * @brief A simple copy on write container that will be used to improve slot lists
 * access efficiency in a multithreaded context.
 */
template<typename T>
class copy_on_write {
    struct payload {
        payload() = default;

        template<typename... Args>
        explicit payload(Args&&... args)
            : value(std::forward<Args>(args)...) {}

        // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
        std::atomic<std::size_t> count{1};
        T value{}; // NOLINT(misc-non-private-member-variables-in-classes)
    };

public:
    using element_type = T;

    copy_on_write()
        : m_data(new payload) {}

    template<typename U>
        requires(!std::same_as<std::decay_t<U>, copy_on_write>)
    explicit copy_on_write(U&& x)
        : m_data(new payload(std::forward<U>(x))) {}

    copy_on_write(const copy_on_write& x) noexcept
        : m_data(x.m_data) {
        ++m_data->count;
    }

    copy_on_write(copy_on_write&& x) noexcept
        : m_data(x.m_data) {
        x.m_data = nullptr;
    }

    ~copy_on_write() {
        if (m_data && (--m_data->count == 0)) {
            delete m_data;
            m_data = nullptr;
        }
    }

    copy_on_write& operator=(const copy_on_write& x) noexcept {
        if (&x != this) {
            *this = copy_on_write(x);
        }
        return *this;
    }

    copy_on_write& operator=(copy_on_write&& x) noexcept {
        auto tmp = std::move(x);
        swap(*this, tmp);
        return *this;
    }

    element_type& write() {
        if (!unique()) {
            *this = copy_on_write(read());
        }
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDelete)
        return m_data->value; // TODO(CK) error: Use of memory after it is freed?
    }

    [[nodiscard]] const element_type& read() const noexcept {
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDelete)
        return m_data->value; // TODO(CK) error: Use of memory after it is freed?
    }

    friend inline void swap(copy_on_write& x, copy_on_write& y) noexcept {
        std::swap(x.m_data, y.m_data);
    }

private:
    [[nodiscard]] bool unique() const noexcept { return m_data->count == 1; }

private:
    payload* m_data;
};

/**
 * @brief Specializations for thread-safe code path
 */
template<typename T>
const T& cow_read(const T& v) {
    return v;
}

template<typename T>
const T& cow_read(copy_on_write<T>& v) {
    return v.read();
}

// Simple wrapper to provide uniform interface for non-RCU case
template<typename T>
class ref_write_guard {
public:
    explicit ref_write_guard(T& ref) : m_ref(ref) {}
    T& get() { return m_ref; }
private:
    T& m_ref;
};

template<typename T>
ref_write_guard<T> cow_write(T& v) {
    return ref_write_guard<T>(v);
}

template<typename T>
T& cow_write(copy_on_write<T>& v) {
    return v.write();
}

/**
 * @brief RCU-style lock-free copy-on-write container.
 * 
 * Implements Read-Copy-Update pattern:
 * - Readers: Lock-free atomic load of shared_ptr (keeps data alive during use)
 * - Writers: Copy current data, modify copy, atomically publish new version
 * 
 * The shared_ptr reference counting provides automatic deferred reclamation:
 * old data is freed when the last reader releases their shared_ptr.
 * 
 * Uses std::atomic_load/store free functions for portability across all
 * C++20 standard library implementations (some don't support atomic<shared_ptr>).
 * 
 * See: https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic
 * See: https://en.wikipedia.org/wiki/Read-copy-update
 */

// Suppress deprecation warnings for std::atomic_*() free functions on shared_ptr.
// We intentionally use these deprecated functions for portability: std::atomic<shared_ptr<T>>
// (C++20 P0718R2) is not yet reliably supported across all standard library implementations.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)  // STL4029: std::atomic_*() for shared_ptr deprecated
#elif defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

template<typename T>
class rcu_cow {
public:
    using element_type = T;

    rcu_cow()
        : m_published(std::make_shared<T>()) {}

    template<typename U>
        requires(!std::same_as<std::decay_t<U>, rcu_cow>)
    explicit rcu_cow(U&& x)
        : m_published(std::make_shared<T>(std::forward<U>(x))) {}

    // Copy: share the published snapshot
    rcu_cow(const rcu_cow& x) noexcept
        : m_published(std::atomic_load_explicit(&x.m_published, std::memory_order_acquire)) {}

    // Move: take ownership of published snapshot
    rcu_cow(rcu_cow&& x) noexcept
        : m_published(std::atomic_exchange_explicit(&x.m_published, 
                      std::make_shared<T>(), std::memory_order_acq_rel)) {}

    ~rcu_cow() = default;

    rcu_cow& operator=(const rcu_cow& x) noexcept {
        if (&x != this) {
            std::atomic_store_explicit(&m_published,
                std::atomic_load_explicit(&x.m_published, std::memory_order_acquire),
                std::memory_order_release);
        }
        return *this;
    }

    rcu_cow& operator=(rcu_cow&& x) noexcept {
        if (&x != this) {
            std::atomic_store_explicit(&m_published,
                std::atomic_exchange_explicit(&x.m_published, 
                    std::make_shared<T>(), std::memory_order_acq_rel),
                std::memory_order_release);
        }
        return *this;
    }

    /**
     * @brief Lock-free read of current published data.
     * @return shared_ptr that keeps the data alive during use
     * 
     * This is the "Read" in RCU. The returned shared_ptr ensures the
     * data remains valid even if a writer publishes a new version.
     */
    [[nodiscard]] std::shared_ptr<const T> read() const noexcept {
        return std::atomic_load_explicit(&m_published, std::memory_order_acquire);
    }

    /**
     * @brief Begin a write operation by copying current data.
     * @return shared_ptr to a mutable copy for modification
     * @note Must be called under external lock protection.
     * 
     * This is the "Copy" in RCU. Returns a private mutable copy.
     * After modifications, call publish() to make changes visible.
     */
    [[nodiscard]] std::shared_ptr<T> copy_for_write() const {
        auto current = std::atomic_load_explicit(&m_published, std::memory_order_acquire);
        return std::make_shared<T>(*current);
    }

    /**
     * @brief Publish a modified copy, making it visible to readers.
     * @param new_data The modified data to publish
     * @note Must be called under external lock protection.
     * 
     * This is the "Update" in RCU. Atomically publishes the new version.
     * Old readers continue using their snapshot safely.
     */
    void publish(std::shared_ptr<T> new_data) {
        std::atomic_store_explicit(&m_published, std::move(new_data), std::memory_order_release);
    }
    
    /**
     * @brief Lock-free CAS publish for concurrent writers.
     * @param expected The snapshot we copied from (updated on failure)
     * @param new_data The modified data to publish
     * @return true if publish succeeded, false if expected was stale
     * 
     * This enables lock-free connect/disconnect by allowing multiple writers
     * to race. On failure, expected is updated to the current value so the
     * caller can retry with a fresh copy.
     */
    bool try_publish(std::shared_ptr<const T>& expected, std::shared_ptr<T> new_data) {
        // Cast to non-const for the compare_exchange (we're replacing the whole ptr)
        auto expected_nonconst = std::const_pointer_cast<T>(expected);
        bool success = std::atomic_compare_exchange_strong_explicit(
            &m_published,
            &expected_nonconst,
            std::move(new_data),
            std::memory_order_acq_rel,
            std::memory_order_acquire);
        if (!success) {
            // Update expected with current value for retry
            expected = std::const_pointer_cast<const T>(expected_nonconst);
        }
        return success;
    }

    friend inline void swap(rcu_cow& x, rcu_cow& y) noexcept {
        auto tmp = std::atomic_load_explicit(&x.m_published, std::memory_order_acquire);
        std::atomic_store_explicit(&x.m_published,
            std::atomic_load_explicit(&y.m_published, std::memory_order_acquire),
            std::memory_order_release);
        std::atomic_store_explicit(&y.m_published, tmp, std::memory_order_release);
    }

private:
    // Use plain shared_ptr with atomic free functions for portability.
    // std::atomic<shared_ptr<T>> (C++20 P0718R2) is not yet supported by all implementations.
    std::shared_ptr<T> m_published;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/**
 * @brief RAII guard for RCU write operations.
 * 
 * Holds a mutable copy during modification and publishes on destruction.
 * This ensures the copy-update cycle completes even if an exception occurs.
 */
template<typename T>
class rcu_write_guard {
public:
    rcu_write_guard(rcu_cow<T>& cow) 
        : m_cow(cow)
        , m_copy(cow.copy_for_write()) {}
    
    ~rcu_write_guard() {
        if (m_copy) {
            m_cow.publish(std::move(m_copy));
        }
    }

    // Non-copyable, non-movable
    rcu_write_guard(const rcu_write_guard&) = delete;
    rcu_write_guard& operator=(const rcu_write_guard&) = delete;
    rcu_write_guard(rcu_write_guard&&) = delete;
    rcu_write_guard& operator=(rcu_write_guard&&) = delete;

    T& get() { return *m_copy; }
    T* operator->() { return m_copy.get(); }
    T& operator*() { return *m_copy; }

private:
    rcu_cow<T>& m_cow;
    std::shared_ptr<T> m_copy;
};

// Specializations for rcu_cow
template<typename T>
std::shared_ptr<const T> cow_read(const rcu_cow<T>& v) {
    return v.read();
}

template<typename T>
rcu_write_guard<T> cow_write(rcu_cow<T>& v) {
    return rcu_write_guard<T>(v);
}

// cow_read for shared_ptr (dereference to get the value)
template<typename T>
const T& cow_read(const std::shared_ptr<const T>& v) {
    return *v;
}

// Slot allocation strategies
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
/**
 * @brief Create slot using intrusive reference counting
 * 
 * With intrusive_ptr, the reference count is embedded in the object,
 * eliminating the separate control block. This enables true arena allocation.
 */

#if defined(SIGSLOT_USE_SLOT_POOL) && SIGSLOT_USE_SLOT_POOL == 2
// Intrusive + Arena: fastest combination
template<typename B, typename D, typename... Arg>
inline intrusive_ptr<B> make_slot_ptr(Arg&&... arg) {
    // Allocate from arena, construct with placement new
    auto& arena = get_slot_arena();
    void* mem = arena.allocate(sizeof(D), alignof(D));
    D* ptr = new(mem) D(std::forward<Arg>(arg)...);
    ptr->set_arena_allocated(); // Mark so destructor doesn't call delete
    return intrusive_ptr<B>(static_cast<B*>(ptr), true);
}
#else
// Intrusive only: regular heap allocation
template<typename B, typename D, typename... Arg>
inline intrusive_ptr<B> make_slot_ptr(Arg&&... arg) {
    return static_pointer_cast<B>(make_intrusive<D>(std::forward<Arg>(arg)...));
}
#endif

#elif defined(SIGSLOT_USE_SLOT_POOL)
/**
 * @brief Thread-local memory pool for slot allocations
 * 
 * Two strategies available:
 * 1. ARENA (SIGSLOT_USE_SLOT_POOL=2): Custom bump-pointer arena
 *    - Fastest: ~5-10ns allocation
 *    - No per-allocation bookkeeping
 *    - Best for high connect/disconnect churn
 * 
 * 2. PMR (SIGSLOT_USE_SLOT_POOL=1): std::pmr::unsynchronized_pool_resource
 *    - Portable: Standard C++17
 *    - Good performance: ~20-30ns allocation
 *    - Automatic memory management
 */

#if SIGSLOT_USE_SLOT_POOL == 2
// Arena allocator strategy
template<typename B, typename D, typename... Arg>
inline std::shared_ptr<B> make_slot_ptr(Arg&&... arg) {
    return std::static_pointer_cast<B>(
        detail::make_shared_arena<D>(std::forward<Arg>(arg)...)
    );
}

#else
// PMR pool strategy
struct slot_pool_holder {
    std::pmr::unsynchronized_pool_resource pool;
    
    slot_pool_holder() : pool(std::pmr::pool_options{
        .max_blocks_per_chunk = 32,      // Reasonable chunk size
        .largest_required_pool_block = 256  // Most slots are < 256 bytes
    }) {}
    
    std::pmr::memory_resource* get() noexcept { return &pool; }
};

inline std::pmr::memory_resource* get_slot_pool() {
    thread_local slot_pool_holder holder;
    return holder.get();
}

template<typename B, typename D, typename... Arg>
inline std::shared_ptr<B> make_slot_ptr(Arg&&... arg) {
    std::pmr::polymorphic_allocator<D> alloc(get_slot_pool());
    return std::static_pointer_cast<B>(
        std::allocate_shared<D>(alloc, std::forward<Arg>(arg)...)
    );
}
#endif // SIGSLOT_USE_SLOT_POOL == 2

#else // !SIGSLOT_USE_SLOT_POOL && !SIGSLOT_USE_INTRUSIVE_PTR

/**
 * @brief std::make_shared instantiates a lot a templates, and makes both compilation time
 * and executable size far bigger than they need to be. We offer a make_shared
 * equivalent that will avoid most instantiations with the following tradeoffs:
 * - Not exception safe,
 * - Allocates a separate control block, and will thus make the code slower.
 */
#ifdef SIGSLOT_REDUCE_COMPILE_TIME
template<typename B, typename D, typename... Arg>
inline std::shared_ptr<B> make_slot_ptr(Arg&&... arg) {
    return std::shared_ptr<B>(static_cast<B*>(new D(std::forward<Arg>(arg)...)));
}
#else
template<typename B, typename D, typename... Arg>
inline std::shared_ptr<B> make_slot_ptr(Arg&&... arg) {
    return std::static_pointer_cast<B>(std::make_shared<D>(std::forward<Arg>(arg)...));
}
#endif

#endif // SIGSLOT_USE_INTRUSIVE_PTR / SIGSLOT_USE_SLOT_POOL

/** @brief slot_state holds slot type independent state, to be used to interact with
 * slots indirectly through connection and scoped_connection objects.
 * 
 * When SIGSLOT_USE_INTRUSIVE_PTR is enabled:
 * - Inherits from intrusive_refcount for SBO storage performance
 * - Stores a self-referencing shared_ptr for safe weak pointer support
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

    // memory_order_relaxed is safe here: we only need eventual consistency for
    // the connected flag. No synchronization with other memory operations required.
    // See: https://en.cppreference.com/w/cpp/atomic/memory_order
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

    // Blocking is a hint to skip slot invocation; relaxed ordering suffices.
    [[nodiscard]] bool blocked() const noexcept {
        return m_blocked.load(std::memory_order_relaxed);
    }
    void block() noexcept { m_blocked.store(true, std::memory_order_relaxed); }
    void unblock() noexcept { m_blocked.store(false, std::memory_order_relaxed); }

protected:
    virtual void do_disconnect() {}

    [[nodiscard]] std::size_t index() const noexcept { 
        return m_index.load(std::memory_order_relaxed); 
    }

    void set_index(std::size_t idx) noexcept { 
        m_index.store(idx, std::memory_order_relaxed); 
    }

private:
    template<GroupId, typename, typename...>
    friend class ::sigslot::signal_base;

    std::atomic<std::size_t> m_index; // index into the array of slot pointers inside the signal
    std::atomic<bool> m_connected;
    std::atomic<bool> m_blocked;
};

template<typename Group>
class grouped_slot : public slot_state {
protected:
    explicit grouped_slot(Group const& gid)
        : slot_state()
        , m_group(gid) {}

public:
    [[nodiscard]] Group const& group() const { return m_group; }

private:
    const Group m_group;
};

} // namespace detail

// Type aliases for pointer types based on configuration
#ifdef SIGSLOT_USE_INTRUSIVE_PTR
// With dual-counter intrusive_ptr, we use intrusive_weak_ptr for weak references
// This eliminates the need for std::weak_ptr anchor and provides lock-free lock()
template<typename T>
using slot_weak_ptr = detail::intrusive_weak_ptr<T>;
template<typename T>
using slot_strong_ptr = detail::intrusive_ptr<T>;

template<typename T, typename U>
inline slot_strong_ptr<T> slot_pointer_cast(const slot_strong_ptr<U>& ptr) {
    return detail::static_pointer_cast<T>(ptr);
}

// Helper to get a weak_ptr from a slot for connection objects
template<typename T>
inline slot_weak_ptr<detail::slot_state> get_slot_weak_ptr(const slot_strong_ptr<T>& ptr) {
    return slot_weak_ptr<detail::slot_state>(detail::static_pointer_cast<detail::slot_state>(ptr));
}
#else
template<typename T>
using slot_weak_ptr = std::weak_ptr<T>;
template<typename T>
using slot_strong_ptr = std::shared_ptr<T>;

template<typename T, typename U>
inline slot_strong_ptr<T> slot_pointer_cast(const slot_strong_ptr<U>& ptr) {
    return std::static_pointer_cast<T>(ptr);
}

// Helper to get a weak_ptr from a slot for connection objects
template<typename T>
inline slot_weak_ptr<detail::slot_state> get_slot_weak_ptr(const slot_strong_ptr<T>& ptr) {
    return ptr;
}
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
 @note that connection is not a RAII object, one does not need to hold one
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

protected:
    template<GroupId, typename, typename...>
    friend class signal_base;
    explicit connection(slot_weak_ptr<detail::slot_state> s) noexcept
        : m_state{std::move(s)} {}

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

private:
    template<GroupId, typename, typename...>
    friend class signal_base;
    explicit scoped_connection(slot_weak_ptr<detail::slot_state> s) noexcept
        : connection{std::move(s)} {}
};

/**
 * @brief Observer is a base class for intrusive lifetime tracking of objects.
 *
 * This is an alternative to trackable pointers, such as std::shared_ptr,
 * and manual connection management by keeping connection objects in scope.
 * Deriving from this class allows automatic disconnection of all the slots
 * connected to any signal when an instance is destroyed.
 */
template<typename Lockable>
struct observer_base : private detail::observer_type {
    virtual ~observer_base() = default;

protected:
    /**
     * @brief Disconnect all signals connected to this object.
     *
     * To avoid invocation of slots on a semi-destructed instance, which may happen
     * in multi-threaded contexts, derived classes should call this method in their
     * destructor. This will ensure proper disconnection prior to the destruction.
     */
    void disconnect_all() {
        std::unique_lock<Lockable> _{m_mutex};
        m_connections.clear();
    }

private:
    template<GroupId, typename, typename...>
    friend class signal_base;

    void add_connection(connection conn) {
        std::unique_lock<Lockable> _{m_mutex};
        m_connections.emplace_back(std::move(conn));
    }

    Lockable m_mutex;
    std::vector<scoped_connection> m_connections;
};

/**
 * @brief Specialization of observer_base to be used in single threaded contexts.
 */
using observer_st = observer_base<detail::null_mutex>;

/**
 * @brief Specialization of observer_base to be used in multi-threaded contexts.
 */
using observer = observer_base<std::mutex>;


namespace detail {

//concept requirements for signal_interface

template<typename Sig, typename... Args>
concept ConnectCallable = requires(Sig sig, Args&&... args) {
    { sig.connect(std::forward<Args>(args)...) } -> std::same_as<sigslot::connection>;
};
template<typename Sig, typename... Args>
concept ConnectExtendedCallable = requires(Sig sig, Args&&... args) {
    { sig.connect_extended(std::forward<Args>(args)...) } -> std::same_as<sigslot::connection>;
};

template<typename Sig, typename... Args>
concept DisconnectCallable = requires(Sig sig, Args&&... args) {
    { sig.disconnect(std::forward<Args>(args)...) } -> std::same_as<size_t>;
};

// interface for cleanable objects, used to cleanup disconnected slots
template<typename Group>
struct cleanable {
    virtual ~cleanable() = default;
    virtual void clean(grouped_slot<Group>*) = 0;
};

template<typename Group, typename...>
class slot_base;

template<typename Group, typename... T>
using slot_ptr = slot_strong_ptr<slot_base<Group, T...>>;

/** @brief A base class for slot objects. This base type only depends on slot argument
 * types, it will be used as an element in an intrusive singly-linked list of
 * slots, hence the public next member.
 */
template<typename Group, typename... Args>
class slot_base : public grouped_slot<Group> {
public:
    using group_id = Group;
    
#ifdef SIGSLOT_USE_SLOT_VARIANT
    // Inline function pointer for direct dispatch (eliminates vtable lookup)
    // Returns true if call succeeded, false if slot expired (tracked slots)
    using call_fn_t = bool(*)(slot_base*, Args...);
    
    explicit slot_base(cleanable<Group>& c, group_id const& gid, call_fn_t call_fn)
        : grouped_slot<Group>(gid)
        , cleaner(c)
        , call_fn_(call_fn) {}
    
    // Fallback constructor using virtual dispatch (for extended slots)
    explicit slot_base(cleanable<Group>& c, group_id const& gid)
        : grouped_slot<Group>(gid)
        , cleaner(c)
        , call_fn_(&slot_base::virtual_dispatch) {}
    
    static bool virtual_dispatch(slot_base* self, Args... args) {
        self->call_slot(args...);
        return true;
    }
#else
    explicit slot_base(cleanable<Group>& c, group_id const& gid)
        : grouped_slot<Group>(gid)
        , cleaner(c) {}
#endif
    ~slot_base() override = default;

    // method effectively responsible for calling the "slot" function with
    // supplied arguments whenever emission happens.
    virtual void call_slot(Args...) = 0;

    template<typename... U>
    void operator()(U&&... u) {
        if (slot_state::connected() && !slot_state::blocked()) {
#ifdef SIGSLOT_USE_SLOT_VARIANT
            // Direct function pointer call - no vtable lookup
            call_fn_(this, std::forward<U>(u)...);
#else
            call_slot(std::forward<U>(u)...); // NOLINT(hicpp-no-array-decay)
#endif
        }
    }

    // check if we are storing callable c
    template<typename C>
    [[nodiscard]] [[nodiscard]] [[nodiscard]] [[nodiscard]] bool has_callable(const C& c) const {
        auto cp = get_function_ptr(c);
        auto p = get_callable();
        return cp && p && cp == p;
    }

    /**
     * @brief Check if this slot holds the given callable.
     * 
     * Uses portable byte comparison of function pointers via func_ptr.
     * No RTTI required - works on all compilers including clang-cl with multiple inheritance.
     */
    template<typename C>
    [[nodiscard]] bool has_full_callable(const C& c) const {
        return has_callable(c);
    }

    // check if we are storing object o
    template<typename O>
    [[nodiscard]] [[nodiscard]] bool has_object(const O& o) const {
        return get_object() == get_object_ptr(o);
    }

protected:
    void do_disconnect() final { cleaner.clean(this); }

    // retieve a pointer to the object embedded in the slot
    [[nodiscard]] virtual obj_ptr get_object() const noexcept { return nullptr; }

    // retrieve a pointer to the callable embedded in the slot
    [[nodiscard]] virtual func_ptr get_callable() const noexcept {
        return get_function_ptr(nullptr);
    }

private:
    cleanable<Group>& cleaner;
#ifdef SIGSLOT_USE_SLOT_VARIANT
    call_fn_t call_fn_;
#endif
};

/**
 * @brief A slot object holds state information, and a callable to to be called
 * whenever the function call operator of its slot_base base class is called.
 */
template<typename Group, typename Func, typename... Args>
class slot final : public slot_base<Group, Args...> {
    using base_t = slot_base<Group, Args...>;
public:
    template<typename F>
    constexpr slot(cleanable<Group>& c, F&& f, Group const& gid)
#ifdef SIGSLOT_USE_SLOT_VARIANT
        : base_t(c, gid, &slot::call_fn_impl)
#else
        : base_t(c, gid)
#endif
        , func{std::forward<F>(f)} {}

#ifdef SIGSLOT_USE_SLOT_VARIANT
    static bool call_fn_impl(base_t* self, Args... args) {
        static_cast<slot*>(self)->func(args...);
        return true;
    }
#endif

protected:
    void call_slot(Args... args) override { func(args...); }

    [[nodiscard]] func_ptr get_callable() const noexcept override { return get_function_ptr(func); }

private:
    std::decay_t<Func> func;
};

/**
 * @brief Variation of slot that prepends a connection object to the callable
 */
template<typename Group, typename Func, typename... Args>
class slot_extended final : public slot_base<Group, Args...> {
    using base_t = slot_base<Group, Args...>;
public:
    template<typename F>
    constexpr slot_extended(cleanable<Group>& c, F&& f, Group const& gid)
        // Note: Extended slots use virtual dispatch (connection dependency prevents inline fn ptr)
        : base_t(c, gid)
        , func{std::forward<F>(f)} {}

    connection conn; // TODO(CK): prevent public members!

protected:
    void call_slot(Args... args) override { func(conn, args...); }

    [[nodiscard]] func_ptr get_callable() const noexcept override { return get_function_ptr(func); }

private:
    std::decay_t<Func> func;
};

/**
 * @brief A slot object holds state information, an object and a pointer over member
 * function to be called whenever the function call operator of its slot_base
 * base class is called.
 */
template<typename Group, typename Pmf, typename Ptr, typename... Args>
class slot_pmf final : public slot_base<Group, Args...> {
    using base_t = slot_base<Group, Args...>;
public:
    template<typename F, typename P>
    constexpr slot_pmf(cleanable<Group>& c, F&& f, P&& p, Group const& gid)
#ifdef SIGSLOT_USE_SLOT_VARIANT
        : base_t(c, gid, &slot_pmf::call_fn_impl)
#else
        : base_t(c, gid)
#endif
        , pmf{std::forward<F>(f)}
        , ptr{std::forward<P>(p)} {}

#ifdef SIGSLOT_USE_SLOT_VARIANT
    static bool call_fn_impl(base_t* self, Args... args) {
        auto* s = static_cast<slot_pmf*>(self);
        ((*s->ptr).*s->pmf)(args...);
        return true;
    }
#endif

protected:
    void call_slot(Args... args) override { ((*ptr).*pmf)(args...); }

    [[nodiscard]] func_ptr get_callable() const noexcept override { return get_function_ptr(pmf); }

    [[nodiscard]] obj_ptr get_object() const noexcept override { return get_object_ptr(ptr); }

private:
    std::decay_t<Pmf> pmf;
    std::decay_t<Ptr> ptr;
};

/**
 * @brief Variation of slot that prepends a connection object to the callable
 */
template<typename Group, typename Pmf, typename Ptr, typename... Args>
class slot_pmf_extended final : public slot_base<Group, Args...> {
    using base_t = slot_base<Group, Args...>;
public:
    template<typename F, typename P>
    constexpr slot_pmf_extended(cleanable<Group>& c, F&& f, P&& p, Group const& gid)
        // Note: Extended slots use virtual dispatch (connection dependency prevents inline fn ptr)
        : base_t(c, gid)
        , pmf{std::forward<F>(f)}
        , ptr{std::forward<P>(p)} {}

    connection conn; // TODO(CK): prevent public members!

protected:
    void call_slot(Args... args) override { ((*ptr).*pmf)(conn, args...); }

    [[nodiscard]] func_ptr get_callable() const noexcept override { return get_function_ptr(pmf); }
    [[nodiscard]] obj_ptr get_object() const noexcept override { return get_object_ptr(ptr); }

private:
    std::decay_t<Pmf> pmf;
    std::decay_t<Ptr> ptr;
};

/**
 * @brief An implementation of a slot that tracks the life of a supplied object
 * through a weak pointer in order to automatically disconnect the slot
 * on said object destruction.
 */
template<typename Group, typename Func, typename WeakPtr, typename... Args>
class slot_tracked final : public slot_base<Group, Args...> {
    using base_t = slot_base<Group, Args...>;
public:
    template<typename F, typename P>
    constexpr slot_tracked(cleanable<Group>& c, F&& f, P&& p, Group const& gid)
#ifdef SIGSLOT_USE_SLOT_VARIANT
        : base_t(c, gid, &slot_tracked::call_fn_impl)
#else
        : base_t(c, gid)
#endif
        , func{std::forward<F>(f)}
        , ptr{std::forward<P>(p)} {}

#ifdef SIGSLOT_USE_SLOT_VARIANT
    static bool call_fn_impl(base_t* self, Args... args) {
        auto* s = static_cast<slot_tracked*>(self);
        auto sp = s->ptr.lock();
        if (!sp) {
            s->disconnect();
            return false;
        }
        if (s->connected()) {
            s->func(args...);
        }
        return true;
    }
#endif

    [[nodiscard]] bool connected() const noexcept override {
        return !ptr.expired() && slot_state::connected();
    }

protected:
    void call_slot(Args... args) override {
        auto sp = ptr.lock();
        if (!sp) {
            slot_state::disconnect();
            return;
        }
        if (slot_state::connected()) {
            func(args...);
        }
    }

    [[nodiscard]] func_ptr get_callable() const noexcept override { return get_function_ptr(func); }

    [[nodiscard]] obj_ptr get_object() const noexcept override { return get_object_ptr(ptr); }

private:
    std::decay_t<Func> func;
    std::decay_t<WeakPtr> ptr;
};

/**
 * @brief An implementation of a slot as a pointer over member function, that tracks
 * the life of a supplied object through a weak pointer in order to automatically
 * disconnect the slot on said object destruction.
 */
template<typename Group, typename Pmf, typename WeakPtr, typename... Args>
class slot_pmf_tracked final : public slot_base<Group, Args...> {
    using base_t = slot_base<Group, Args...>;
public:
    template<typename F, typename P>
    constexpr slot_pmf_tracked(cleanable<Group>& c, F&& f, P&& p, Group const& gid)
#ifdef SIGSLOT_USE_SLOT_VARIANT
        : base_t(c, gid, &slot_pmf_tracked::call_fn_impl)
#else
        : base_t(c, gid)
#endif
        , pmf{std::forward<F>(f)}
        , ptr{std::forward<P>(p)} {}

#ifdef SIGSLOT_USE_SLOT_VARIANT
    static bool call_fn_impl(base_t* self, Args... args) {
        auto* s = static_cast<slot_pmf_tracked*>(self);
        auto sp = s->ptr.lock();
        if (!sp) {
            s->disconnect();
            return false;
        }
        if (s->connected()) {
            ((*sp).*s->pmf)(args...);
        }
        return true;
    }
#endif

    [[nodiscard]] bool connected() const noexcept override {
        return !ptr.expired() && slot_state::connected();
    }

protected:
    void call_slot(Args... args) override {
        auto sp = ptr.lock();
        if (!sp) {
            slot_state::disconnect();
            return;
        }
        if (slot_state::connected()) {
            ((*sp).*pmf)(args...);
        }
    }

    [[nodiscard]] func_ptr get_callable() const noexcept override { return get_function_ptr(pmf); }

    [[nodiscard]] obj_ptr get_object() const noexcept override { return get_object_ptr(ptr); }

private:
    std::decay_t<Pmf> pmf;
    std::decay_t<WeakPtr> ptr;
};

} // namespace detail


/**
 * @brief signal_base is an implementation of the observer pattern, through the use
 * of an emitting object and slots that are connected to the signal and called
 * with supplied arguments when a signal is emitted.
 *
 * signal_base is the general implementation, whose locking policy must be
 * set in order to decide thread safety guarantees. signal and signal_st
 * are partial specializations for multi-threaded and single-threaded use.
 *
 * It does not allow slots to return a value.
 *
 * Slot execution order can be constrained by assigning group ids to the slots.
 * The execution order of slots in a same group is unspecified and should not be
 * relied upon, however groups are executed in ascending group ids order. When
 * the group id of a slot is not set, it is assigned to the group 0. Group ids
 * can have any value in the range of signed 32 bit integers.
 *
 * @tparam Lockable a lock type to decide the lock policy
 * @tparam T... the argument types of the emitting and slots functions.
 */
template<GroupId Group, typename Lockable, typename... T>
class signal_base final : public detail::cleanable<Group> {
public:
    using group_id = Group;
    using value_type = std::tuple<T...>;
    using connection = sigslot::connection;
    static constexpr bool is_thread_safe = !std::same_as<Lockable, detail::null_mutex>;

private:
    // For thread-safe signals, use rcu_cow for lock-free emission.
    // For non-thread-safe signals, store the list directly.
    template<typename U>
    using cow_type = std::conditional_t<is_thread_safe, detail::rcu_cow<U>, U>;

    // For reading: thread-safe returns shared_ptr (lock-free), non-thread-safe returns const ref
    template<typename U>
    using cow_copy_type = std::conditional_t<is_thread_safe, std::shared_ptr<const U>, const U&>;

    // Note: lock_type removed - all operations are now lock-free
    using slot_base = detail::slot_base<group_id, T...>;
    using slot_ptr = detail::slot_ptr<Group, T...>;
    using slots_type = detail::sbo_container<slot_ptr, 3>; // SBO for up to 3 slots
    struct group_type {
        slots_type slts;
        group_id gid;
        
        // SBO-specific optimizations
        constexpr bool is_using_heap() const noexcept { return slts.is_using_heap(); }
        constexpr std::size_t heap_threshold() const noexcept { return 3; }
        constexpr std::span<slot_ptr> get_slots_span() noexcept { return slts.get_span(); }
        constexpr std::span<const slot_ptr> get_slots_span() const noexcept { return slts.get_span(); }
    };
    using list_type = std::vector<group_type>; // kept ordered by ascending gid

public:
    signal_base() noexcept
        : m_block(false) {}
    ~signal_base() override { disconnect_all(); }

    signal_base(const signal_base&) = delete;
    signal_base& operator=(const signal_base&) = delete;

    // Lock-free move constructor using RCU swap
    signal_base(signal_base&& o) noexcept
        : m_block{o.m_block.load(std::memory_order_relaxed)} {
        swap(m_slots, o.m_slots);  // RCU atomic swap
    }

    // Lock-free move assignment using RCU swap
    signal_base& operator=(signal_base&& o) noexcept {
        if (this != &o) {
            swap(m_slots, o.m_slots);  // RCU atomic swap
            m_block.store(o.m_block.exchange(m_block.load(std::memory_order_relaxed), 
                         std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    /**
     * @brief Emit a signal
     *
     * Effect: All non blocked and connected slot functions will be called
     *         with supplied arguments.
     * Safety: With proper locking (see pal::signal), emission can happen from
     *         multiple threads simultaneously. The guarantees only apply to the
     *         signal object, it does not cover thread safety of potentially
     *         shared state used in slot functions.
     *
     * @param a... arguments to emit
     */
    template<typename Self, typename... U>
    void operator()(this Self&& self, U&&... a) {
        // Relaxed load is sufficient: blocking is advisory and doesn't require
        // synchronization with slot list modifications (handled by COW + mutex).
        if (self.m_block.load(std::memory_order_relaxed)) {
            return;
        }

        // Reference to the slots to execute them out of the lock
        // a copy may occur if another thread writes to it.
        cow_copy_type<list_type> ref = std::forward<Self>(self).slots_reference();

        for (const auto& group : detail::cow_read(ref)) {
            // SBO-optimized iteration: use span for better cache locality
            if (!group.is_using_heap()) {
                // Fast path: stack storage, use span for optimal iteration
                for (const auto& s : group.get_slots_span()) {
                    s->operator()(std::forward<U>(a)...);
                }
            } else {
                // Fallback: heap storage, use standard iteration
                for (const auto& s : group.slts) {
                    s->operator()(std::forward<U>(a)...);
                }
            }
        }
    }

    /**
     * @brief RAII batch emitter for optimized sequential emissions
     * 
     * Caches the slot snapshot to avoid repeated atomic loads during
     * rapid sequential emissions. Ideal for reactive pipelines.
     * 
     * Usage:
     * @code
     *   {
     *       auto batch = sig.batch();
     *       for (int i = 0; i < 1000; ++i) {
     *           batch(i);  // Uses cached snapshot via operator()
     *       }
     *   }
     * @endcode
     */
    class batch_emitter {
        cow_copy_type<list_type> m_snapshot;
        const std::atomic<bool>* m_block;
        
    public:
        explicit batch_emitter(cow_copy_type<list_type> snapshot, const std::atomic<bool>& block)
            : m_snapshot(std::move(snapshot)), m_block(&block) {}
        
        batch_emitter(const batch_emitter&) = delete;
        batch_emitter& operator=(const batch_emitter&) = delete;
        batch_emitter(batch_emitter&&) = default;
        batch_emitter& operator=(batch_emitter&&) = default;
        
        /**
         * @brief Emit a value using the cached snapshot
         * 
         * Note: Named 'batch_emit' instead of 'emit' to avoid conflict with Qt's emit macro.
         */
        template<typename... U>
        void batch_emit(U&&... a) const {
            if (m_block->load(std::memory_order_relaxed)) {
                return;
            }
            
            for (const auto& group : detail::cow_read(m_snapshot)) {
                if (!group.is_using_heap()) {
                    for (const auto& s : group.get_slots_span()) {
                        s->operator()(std::forward<U>(a)...);
                    }
                } else {
                    for (const auto& s : group.slts) {
                        s->operator()(std::forward<U>(a)...);
                    }
                }
            }
        }
        
        /**
         * @brief Emit a value (operator() alias for batch_emit())
         */
        template<typename... U>
        void operator()(U&&... a) const {
            batch_emit(std::forward<U>(a)...);
        }
    };
    
    /**
     * @brief Create a batch emitter for optimized sequential emissions
     * 
     * Returns an RAII object that caches the slot snapshot. Multiple
     * emissions through the batch emitter avoid repeated atomic loads,
     * improving throughput for reactive pipelines.
     * 
     * The snapshot is consistent for the lifetime of the batch_emitter.
     * New connections made after batch() is called won't be visible.
     * 
     * @return batch_emitter object for emitting values
     */
    template<typename Self>
    [[nodiscard]] auto batch(this Self&& self) {
        return batch_emitter(std::forward<Self>(self).slots_reference(), self.m_block);
    }

    /**
     * @brief Connect a callable of compatible arguments
     *
     * Effect: Creates and stores a new slot responsible for executing the
     *         supplied callable for every subsequent signal emission.
     * Safety: Thread-safety depends on locking policy.
     *
     * @param c a callable
     * @param gid an identifier that can be used to order slot execution
     * @return a connection object that can be used to interact with the slot
     */
    template<typename Callable>
        requires trait::Callable<Callable, T...>
    connection connect(Callable&& c, group_id gid = group_id{}) {
        using slot_t = detail::slot<group_id, Callable, T...>;
        auto s = make_slot<slot_t>(std::forward<Callable>(c), gid);
        connection conn(get_slot_weak_ptr(s));
        add_slot(std::move(s));
        return conn;
    }

    /**
     * @brief Connect a callable with an additional connection argument
     *
     * The callable's first argument must be of type connection. This overload
     * the callable to manage it's own connection through this argument.
     *
     * @param c a callable
     * @param gid an identifier that can be used to order slot execution
     * @return a connection object that can be used to interact with the slot
     */
    template<typename Callable>
        requires trait::Callable<Callable, connection&, T...>
    connection connect_extended(Callable&& c, group_id gid = group_id{}) {
        using slot_t = detail::slot_extended<group_id, Callable, T...>;
        auto s = make_slot<slot_t>(std::forward<Callable>(c), gid);
        connection conn(get_slot_weak_ptr(s));
        slot_pointer_cast<slot_t>(s)->conn = conn;
        add_slot(std::move(s));
        return conn;
    }

    /**
     * @brief Overload of connect for pointers over member functions derived from
     * observer
     *
     * @param pmf a pointer over member function
     * @param ptr an object pointer derived from observer
     * @param gid an identifier that can be used to order slot execution
     * @return a connection object that can be used to interact with the slot
     */
    template<typename Pmf, trait::Observer Ptr>
        requires trait::MemberCallable<Pmf, Ptr, T...>
    connection connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        using slot_t = detail::slot_pmf<group_id, Pmf, Ptr, T...>;
        auto s = make_slot<slot_t>(std::forward<Pmf>(pmf), std::forward<Ptr>(ptr), gid);
        connection conn(get_slot_weak_ptr(s));
        add_slot(std::move(s));
        ptr->add_connection(conn);
        return conn;
    }

    /**
     * @brief Overload of connect for pointers over member functions
     *
     * @param pmf a pointer over member function
     * @param ptr an object pointer
     * @param gid an identifier that can be used to order slot execution
     * @return a connection object that can be used to interact with the slot
     */
    template<typename Pmf, typename Ptr>
        requires trait::MemberCallable<Pmf, Ptr, T...> &&
                 (!trait::Observer<Ptr> && !trait::WeakPtrCompatible<Ptr>)
    connection connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        using slot_t = detail::slot_pmf<group_id, Pmf, Ptr, T...>;
        auto s = make_slot<slot_t>(std::forward<Pmf>(pmf), std::forward<Ptr>(ptr), gid);
        connection conn(get_slot_weak_ptr(s));
        add_slot(std::move(s));
        return conn;
    }

    /**
     * @brief Overload  of connect for pointer over member functions and
     *
     * @param pmf a pointer over member function
     * @param ptr an object pointer
     * @param gid an identifier that can be used to order slot execution
     * @return a connection object that can be used to interact with the slot
     */
    template<typename Pmf, typename Ptr>
        requires trait::MemberCallable<Pmf, Ptr, connection&, T...> &&
                 (!trait::WeakPtrCompatible<Ptr>)
    connection connect_extended(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        using slot_t = detail::slot_pmf_extended<group_id, Pmf, Ptr, T...>;
        auto s = make_slot<slot_t>(std::forward<Pmf>(pmf), std::forward<Ptr>(ptr), gid);
        connection conn(get_slot_weak_ptr(s));
        slot_pointer_cast<slot_t>(s)->conn = conn;
        add_slot(std::move(s));
        return conn;
    }

    /**
     * @brief Overload of connect for lifetime object tracking and automatic disconnection
     *
     * Ptr must be convertible to an object following a loose form of weak pointer
     * concept, by implementing the ADL-detected conversion function to_weak().
     *
     * This overload covers the case of a pointer over member function and a
     * trackable pointer of that class.
     *
     @note only weak references are stored, a slot does not extend the lifetime
     * of a suppied object.
     *
     * @param pmf a pointer over member function
     * @param ptr a trackable object pointer
     * @param gid an identifier that can be used to order slot execution
     * @return a connection object that can be used to interact with the slot
     */
    template<typename Pmf, trait::WeakPtrCompatible Ptr>
        requires(!trait::Callable<Pmf, T...>)
    connection connect(Pmf&& pmf, Ptr&& ptr, group_id gid = group_id{}) {
        using trait::to_weak;
        auto w = to_weak(std::forward<Ptr>(ptr));
        using slot_t = detail::slot_pmf_tracked<group_id, Pmf, decltype(w), T...>;
        auto s = make_slot<slot_t>(std::forward<Pmf>(pmf), w, gid);
        connection conn(get_slot_weak_ptr(s));
        add_slot(std::move(s));
        return conn;
    }

    /**
     * @brief Overload of connect for lifetime object tracking and automatic disconnection
     *
     * Trackable must be convertible to an object following a loose form of weak
     * pointer concept, by implementing the ADL-detected conversion function to_weak().
     *
     * This overload covers the case of a standalone callable and unrelated trackable
     * object.
     *
     @note only weak references are stored, a slot does not extend the lifetime
     * of a suppied object.
     *
     * @param c a callable
     * @param ptr a trackable object pointer
     * @param gid an identifier that can be used to order slot execution
     * @return a connection object that can be used to interact with the slot
     */
    template<typename Callable, trait::WeakPtrCompatible Trackable>
        requires trait::Callable<Callable, T...>
    connection connect(Callable&& c, Trackable&& ptr, group_id gid = group_id{}) {
        using trait::to_weak;
        auto w = to_weak(std::forward<Trackable>(ptr));
        using slot_t = detail::slot_tracked<group_id, Callable, decltype(w), T...>;
        auto s = make_slot<slot_t>(std::forward<Callable>(c), w, gid);
        connection conn(get_slot_weak_ptr(s));
        add_slot(std::move(s));
        return conn;
    }

    /**
     * @brief Creates a connection whose duration is tied to the return object
     * Use the same semantics as connect
     */
    template<typename... CallArgs>
    scoped_connection connect_scoped(CallArgs&&... args) {
        return connect(std::forward<CallArgs>(args)...);
    }

    /**
     * @brief Disconnect slots bound to a callable
     *
     * Effect: Disconnects all the slots bound to the callable in argument.
     * Safety: Thread-safety depends on locking policy.
     *
     * Works for free functions, static member functions, and pointer-to-member-functions.
     * PMF comparison uses portable byte equality (no RTTI required).
     * Note: Lambdas and function objects cannot be disconnected by callable.
     *
     * @param c a callable
     * @return the number of disconnected slots
     */
    template<typename Callable>
        requires(trait::Callable<Callable, T...> || trait::Callable<Callable, connection&, T...> ||
                 trait::MemFnPointer<Callable>) &&
                detail::function_traits<Callable>::is_disconnectable
    size_t disconnect(const Callable& c) {
        return disconnect_if([&](const auto& s) { return s->has_full_callable(c); });
    }

    /**
     * @brief Disconnect slots bound to this object
     *
     * Effect: Disconnects all the slots bound to the object or tracked object
     *         in argument.
     * Safety: Thread-safety depends on locking policy.
     *
     * The object may be a pointer or trackable object.
     *
     * @param obj an object
     * @return the number of disconnected slots
     */
    template<typename Obj>
        requires(!trait::Callable<Obj, T...> && !trait::Callable<Obj, connection&, T...> &&
                 !trait::MemFnPointer<Obj>)
    size_t disconnect(const Obj& obj) {
        return disconnect_if([&](const auto& s) { return s->has_object(obj); });
    }

    /**
     * @brief Disconnect slots bound both to a callable and object
     *
     * Effect: Disconnects all the slots bound to the callable and object in argument.
     * Safety: Thread-safety depends on locking policy.
     *
     * For naked pointers, the Callable is expected to be a pointer over member
     * function. If obj is trackable, any kind of Callable can be used.
     *
     * @param c a callable
     * @param obj an object
     * @return the number of disconnected slots
     */
    template<typename Callable, typename Obj>
    size_t disconnect(const Callable& c, const Obj& obj) {
        return disconnect_if(
            [&](const auto& s) { return s->has_object(obj) && s->has_callable(c); });
    }

    /**
     * @brief Disconnect slots in a particular group
     *
     * Effect: Disconnects all the slots in the group id in argument.
     * Safety: Thread-safety depends on locking policy.
     *
     * @param gid a group id
     * @return the number of disconnected slots
     */
    size_t disconnect(group_id gid) {
        if constexpr (is_thread_safe) {
            // Lock-free CAS loop
            while (true) {
                auto current = m_slots.read();
                
                size_t count = 0;
                for (const auto& group : *current) {
                    if (group.gid == gid) {
                        count = group.slts.size();
                        break;
                    }
                }
                
                if (count == 0) {
                    return 0;
                }
                
                auto new_groups = std::make_shared<list_type>(*current);
                for (auto& group : *new_groups) {
                    if (group.gid == gid) {
                        group.slts.clear();
                        break;
                    }
                }
                
                if (m_slots.try_publish(current, std::move(new_groups))) {
                    return count;
                }
            }
        } else {
            for (auto& group : m_slots) {
                if (group.gid == gid) {
                    size_t count = group.slts.size();
                    group.slts.clear();
                    return count;
                }
            }
            return 0;
        }
    }

    /**
     * @brief Disconnects all the slots
     * Safety: Thread safety depends on locking policy
     */
    void disconnect_all() {
        if constexpr (is_thread_safe) {
            while (true) {
                auto current = m_slots.read();
                auto new_groups = std::make_shared<list_type>();
                if (m_slots.try_publish(current, std::move(new_groups))) {
                    break;
                }
            }
        } else {
            m_slots.clear();
        }
    }

    /**
     * @brief Blocks signal emission
     * Safety: thread safe
     */
    void block() noexcept { m_block.store(true, std::memory_order_relaxed); }

    /**
     * @brief Blocks all slots in a given group
     * Safety: thread safe
     */
    void block(group_id const& gid) {
        if constexpr (is_thread_safe) {
            auto current = m_slots.read();
            for (const auto& group : *current) {
                if (group.gid == gid) {
                    for (const auto& slt : group.slts) {
                        slt->block();
                    }
                }
            }
        } else {
            for (const auto& group : m_slots) {
                if (group.gid == gid) {
                    for (const auto& slt : group.slts) {
                        slt->block();
                    }
                }
            }
        }
    }

    /**
     * @brief Unblocks signal emission
     * Safety: thread safe
     */
    void unblock() noexcept { m_block.store(false, std::memory_order_relaxed); }

    /**
     * @brief Unblocks all slots in a given group
     * Safety: thread safe
     */
    void unblock(group_id const& gid) {
        if constexpr (is_thread_safe) {
            auto current = m_slots.read();
            for (const auto& group : *current) {
                if (group.gid == gid) {
                    for (const auto& slt : group.slts) {
                        slt->unblock();
                    }
                }
            }
        } else {
            for (const auto& group : m_slots) {
                if (group.gid == gid) {
                    for (const auto& slt : group.slts) {
                        slt->unblock();
                    }
                }
            }
        }
    }

    /**
     * @brief Tests blocking state of signal emission
     */
    template<typename Self>
    [[nodiscard]] bool blocked(this Self&& self) noexcept {
        return self.m_block.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get number of connected slots
     * Safety: thread safe
     */
    template<typename Self>
    size_t slot_count(this Self&& self) noexcept {
        cow_copy_type<list_type> ref = std::forward<Self>(self).slots_reference();
        size_t count = 0;
        for (const auto& g : detail::cow_read(ref)) {
            count += g.slts.size();
        }
        return count;
    }
    
    // Public SBO interface for performance monitoring
    template<typename Self>
    auto get_sbo_stats(this Self&& self) noexcept {
        return self.get_sbo_stats();
    }
    
    template<typename Self>
    bool is_using_sbo_efficiently(this Self&& self) noexcept {
        auto stats = self.get_sbo_stats();
        return stats.stack_efficiency >= 75.0; // 75%+ slots should use SBO
    }

    /**
     * @brief Check if a callable is connected
     *
     * Effect: Returns true if the callable is connected to this signal.
     * Safety: Thread-safety depends on locking policy.
     *
     * @param c a callable
     * @return true if callable is connected
     */
    template<typename Self, typename Callable>
        requires(trait::Callable<Callable, T...> || trait::Callable<Callable, connection&, T...> ||
                 trait::MemFnPointer<Callable>) &&
                detail::function_traits<Callable>::is_disconnectable
    bool is_connected(this Self&& self, const Callable& c) {
        return std::forward<Self>(self).count_if(
                   [&](const auto& s) { return s->has_full_callable(c); }) > 0;
    }

    /**
     * @brief Check if an object is connected
     *
     * Effect: Returns true if any slot is bound to the object.
     * Safety: Thread-safety depends on locking policy.
     *
     * @param obj an object
     * @return true if object is connected
     */
    template<typename Self, typename Obj>
        requires(!trait::Callable<Obj, T...> && !trait::Callable<Obj, connection&, T...> &&
                 !trait::MemFnPointer<Obj>)
    bool is_connected(this Self&& self, const Obj& obj) {
        return std::forward<Self>(self).count_if(
                   [&](const auto& s) { return s->has_object(obj); }) > 0;
    }

    /**
     * @brief Check if a callable is connected to a specific object
     *
     * Effect: Returns true if the callable is connected to the object.
     * Safety: Thread-safety depends on locking policy.
     *
     * @param c a callable
     * @param obj an object
     * @return true if callable is connected to the object
     */
    template<typename Self, typename Callable, typename Obj>
    bool is_connected(this Self&& self, const Callable& c, const Obj& obj) {
        return std::forward<Self>(self).count_if(
                   [&](const auto& s) { return s->has_object(obj) && s->has_callable(c); }) > 0;
    }

protected:
    /**
     * @brief remove disconnected slots (lock-free)
     */
    void clean(detail::grouped_slot<Group>* state) override {
        const auto idx = state->index();
        const auto& gid = state->group();
        
        if constexpr (is_thread_safe) {
            // Lock-free CAS loop
            while (true) {
                auto current = m_slots.read();
                
                bool found = false;
                for (const auto& group : *current) {
                    if (group.gid == gid) {
                        const auto& slts = group.slts;
                        if (idx < slts.size() && slts[idx] && slts[idx].get() == state) {
                            found = true;
                        }
                        break;
                    }
                }
                
                if (!found) {
                    return;
                }
                
                auto new_groups = std::make_shared<list_type>(*current);
                for (auto& group : *new_groups) {
                    if (group.gid == gid) {
                        auto& slts = group.slts;
                        if (idx < slts.size() && slts[idx] && slts[idx].get() == state) {
                            std::swap(slts[idx], slts.back());
                            if (idx < slts.size() - 1) {
                                slts[idx]->set_index(idx);
                            }
                            slts.pop_back();
                        }
                        break;
                    }
                }
                
                if (m_slots.try_publish(current, std::move(new_groups))) {
                    return;
                }
            }
        } else {
            // Direct modification for non-thread-safe signals
            for (auto& group : m_slots) {
                if (group.gid == gid) {
                    auto& slts = group.slts;
                    if (idx < slts.size() && slts[idx] && slts[idx].get() == state) {
                        std::swap(slts[idx], slts.back());
                        slts[idx]->set_index(idx);
                        slts.pop_back();
                    }
                    return;
                }
            }
        }
    }

private:
    // Lock-free read for thread-safe signals (using rcu_cow).
    // For non-thread-safe signals, just return a const reference.
    template<typename Self>
    inline cow_copy_type<list_type> slots_reference(this Self&& self) {
        if constexpr (is_thread_safe) {
            // Lock-free: rcu_cow::read() returns shared_ptr snapshot
            return self.m_slots.read();
        } else {
            // Non-thread-safe: return const reference directly
            return self.m_slots;
        }
    }

    // create a new slot
    template<typename Slot, typename... A>
    inline auto make_slot(A&&... a) {
        return detail::make_slot_ptr<slot_base, Slot>(*this, std::forward<A>(a)...);
    }

    // add the slot to the list of slots of the right group
    void add_slot(slot_ptr&& s) {
        const group_id& gid = s->group();

        if constexpr (is_thread_safe) {
            // Lock-free CAS loop for thread-safe signals
            while (true) {
                auto current = m_slots.read();
                auto new_groups = std::make_shared<list_type>(*current);
                
                // find the group
                std::size_t group_idx = 0;
                while (group_idx < new_groups->size() && (*new_groups)[group_idx].gid < gid) {
                    group_idx++;
                }

                // create a new group if necessary
                if (group_idx == new_groups->size() || (*new_groups)[group_idx].gid != gid) {
                    new_groups->insert(new_groups->begin() + static_cast<std::ptrdiff_t>(group_idx), 
                                       {{}, gid});
                }

                // add the slot with correct index
                auto& target_group = (*new_groups)[group_idx];
                s->set_index(target_group.slts.size());
                target_group.slts.push_back(std::move(s));
                
                if (m_slots.try_publish(current, new_groups)) {
                    break;  // Success!
                }
                
                // CAS failed - retrieve slot and retry
                s = std::move((*new_groups)[group_idx].slts.back());
                (*new_groups)[group_idx].slts.pop_back();
            }
        } else {
            // Direct modification for non-thread-safe signals
            auto it = m_slots.begin();
            while (it != m_slots.end() && it->gid < gid) {
                it++;
            }
            if (it == m_slots.end() || it->gid != gid) {
                it = m_slots.insert(it, {{}, gid});
            }
            s->set_index(it->slts.size());
            it->slts.push_back(std::move(s));
        }
    }

    // count slots matching a condition (non-destructive)
    template<typename Self, typename Cond>
    size_t count_if(this Self&& self, Cond&& cond) {
        cow_copy_type<list_type> ref = std::forward<Self>(self).slots_reference();
        size_t count = 0;
        for (const auto& group : detail::cow_read(ref)) {
            for (const auto& s : group.slts)
                if (cond(s))
                    ++count;
        }
        return count;
    }
    
    // SBO-specific statistics for performance monitoring
    struct sbo_stats {
        size_t total_groups = 0;
        size_t stack_groups = 0;
        size_t heap_groups = 0;
        size_t total_slots = 0;
        size_t stack_slots = 0;
        size_t heap_slots = 0;
        double stack_efficiency = 0.0; // percentage of slots using SBO
    };
    
    template<typename Self>
    sbo_stats get_sbo_stats(this Self&& self) noexcept {
        cow_copy_type<list_type> ref = std::forward<Self>(self).slots_reference();
        sbo_stats stats{};
        
        for (const auto& group : detail::cow_read(ref)) {
            ++stats.total_groups;
            stats.total_slots += group.slts.size();
            
            if (group.is_using_heap()) {
                ++stats.heap_groups;
                stats.heap_slots += group.slts.size();
            } else {
                ++stats.stack_groups;
                stats.stack_slots += group.slts.size();
            }
        }
        
        if (stats.total_slots > 0) {
            stats.stack_efficiency = (double(stats.stack_slots) / stats.total_slots) * 100.0;
        }
        
        return stats;
    }

    // disconnect a slot if a condition occurs
    template<typename Cond>
    size_t disconnect_if(Cond&& cond) {
        if constexpr (is_thread_safe) {
            // Lock-free CAS loop
            while (true) {
                auto current = m_slots.read();
                
                size_t count = 0;
                for (const auto& group : *current) {
                    for (const auto& slt : group.slts) {
                        if (cond(slt)) {
                            ++count;
                        }
                    }
                }
                
                if (count == 0) {
                    return 0;
                }
                
                auto new_groups = std::make_shared<list_type>(*current);
                size_t actual_count = 0;
                
                for (auto& group : *new_groups) {
                    auto& slts = group.slts;
                    size_t i = 0;
                    while (i < slts.size()) {
                        if (cond(slts[i])) {
                            std::swap(slts[i], slts.back());
                            if (i < slts.size() - 1) {
                                slts[i]->set_index(i);
                            }
                            slts.pop_back();
                            ++actual_count;
                        } else {
                            ++i;
                        }
                    }
                }
                
                if (m_slots.try_publish(current, std::move(new_groups))) {
                    return actual_count;
                }
            }
        } else {
            // Direct modification for non-thread-safe signals
            size_t count = 0;
            for (auto& group : m_slots) {
                auto& slts = group.slts;
                size_t i = 0;
                while (i < slts.size()) {
                    if (cond(slts[i])) {
                        std::swap(slts[i], slts.back());
                        slts[i]->set_index(i);
                        slts.pop_back();
                        ++count;
                    } else {
                        ++i;
                    }
                }
            }
            return count;
        }
    }

    // Helper to clear all slots
    void clear() {
        if constexpr (is_thread_safe) {
            while (true) {
                auto current = m_slots.read();
                auto new_groups = std::make_shared<list_type>();
                if (m_slots.try_publish(current, std::move(new_groups))) {
                    break;
                }
            }
        } else {
            m_slots.clear();
        }
    }

private:
    // Note: m_mutex removed - all operations are now lock-free using CAS on m_slots
    cow_type<list_type> m_slots;
    // Align m_block to its own cache line to prevent false sharing.
    // This ensures that concurrent reads of m_block don't cause cache
    // invalidations when m_slots is modified by other threads.
    // Note: Disabled on MSVC+ASAN - alignas breaks PMF comparison.
    //       Reproduced with VS2022 (17.14) and VS2026 (18.x) + AddressSanitizer.
    // See: https://en.cppreference.com/w/cpp/language/alignas
#if defined(_MSC_VER) && !defined(__clang__) && defined(__SANITIZE_ADDRESS__)
    std::atomic<bool> m_block;
#else
    alignas(sigslot_cache_line_size) std::atomic<bool> m_block;
#endif
};

/**
 * @brief signal_interface wraps a signal and allows only its owner type to invoke it.
 *
 * @tparam Sig The signal template.
 * @tparam Owner The owner type.
 * @tparam Args The signal args.
 */
template<template<typename...> typename Sig, typename Owner, GroupId Group, typename... Args>
class signal_interface final {
    using signal_type = Sig<Group, Args...>;
    std::optional<signal_type> m_sig_storage;
    signal_type* m_sig;
    friend Owner;

    template<typename Self, typename... U>
    inline void operator()(this Self&& self, U&&... args) {
        (*std::forward<Self>(self).m_sig)(std::forward<U>(args)...);
    }

    template<typename Self>
    inline size_t slot_count(this Self&& self) noexcept {
        return std::forward<Self>(self).m_sig->slot_count();
    }

    inline void block() noexcept { m_sig->block(); }

    inline void block(Group const& gid) { m_sig->block(gid); }

    inline void unblock(Group const& gid) { m_sig->unblock(gid); }

    inline void unblock() noexcept { m_sig->unblock(); }

    template<typename Self>
    [[nodiscard]] inline bool blocked(this Self&& self) noexcept {
        return std::forward<Self>(self).m_sig->blocked();
    }

    // NOLINTNEXTLINE(hicpp-noexcept-move,performance-noexcept-move-constructor)
    signal_interface(signal_interface&& o) /* not noexcept */
        : m_sig(nullptr) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        if (o.m_sig_storage.has_value()) {
            m_sig_storage.emplace(std::move(*o.m_sig_storage));
            m_sig = std::addressof(*m_sig_storage);
            o.m_sig_storage.reset();
        } else {
            m_sig = o.m_sig;
        }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        o.m_sig = nullptr;
    }

    // NOLINTNEXTLINE(hicpp-noexcept-move,performance-noexcept-move-constructor)
    signal_interface& operator=(signal_interface&& o) /* not noexcept */ {
        if (m_sig != o.m_sig) {
            if (o.m_sig_storage.has_value()) {
                m_sig_storage = std::move(o.m_sig_storage);
                m_sig = std::addressof(*m_sig_storage);
                o.m_sig = nullptr;
            } else {
                std::swap(m_sig, o.m_sig);
            }
        }
        return *this;
    }

    ~signal_interface() = default;

public:
    using group_id = Group;
    signal_interface()
        : m_sig_storage(std::in_place)
        , m_sig(std::addressof(*m_sig_storage)) {}

    explicit signal_interface(signal_type* sig)
        : m_sig_storage(std::nullopt)
        , m_sig(sig) {}

    signal_interface(signal_interface const&) = delete;
    signal_interface& operator=(signal_interface const&) = delete;

    template<typename... Ts>
        requires detail::ConnectCallable<signal_type, Ts...>
    inline connection connect(Ts&&... args) {
        return m_sig->connect(std::forward<Ts>(args)...);
    }

    template<typename... Ts>
        requires detail::ConnectExtendedCallable<signal_type, Ts...>
    inline connection connect_extended(Ts&&... args) {
        return m_sig->connect_extended(std::forward<Ts>(args)...);
    }

    template<typename... Ts>
        requires detail::ConnectCallable<signal_type, Ts...>
    inline scoped_connection connect_scoped(Ts&&... args) {
        return m_sig->connect(std::forward<Ts>(args)...);
    }

    template<typename... Ts>
        requires detail::DisconnectCallable<signal_type, Ts...>
    inline size_t disconnect(Ts&&... args) {
        return m_sig->disconnect(std::forward<Ts>(args)...);
    }

    inline void disconnect_all() { m_sig->disconnect_all(); }
};

/**
 * @brief Specialization of signal_base to be used in single threaded contexts.
 * Slot connection, disconnection and signal emission are not thread-safe.
 * The performance improvement over the thread-safe variant is not impressive,
 * so this is not very useful.
 */
template<typename... T>
using signal_st = signal_base<int32_t, detail::null_mutex, T...>;

template<GroupId Group, typename... T>
using signal_g_st = signal_base<Group, detail::null_mutex, T...>;

/**
 * @brief Specialization of signal_base to be used in multi-threaded contexts.
 * Slot connection, disconnection and signal emission are thread-safe.
 *
 * Recursive signal emission and emission cycles are supported too.
 */
template<typename... T>
using signal = signal_base<int32_t, std::mutex, T...>;

template<GroupId Group, typename... T>
using signal_g = signal_base<int32_t, std::mutex, T...>;

/**
 * @brief Specialization of signal_interface for single threaded signals.
 *
 * @tparam Owner The owner type. The call operator will only be accessible
 * from this type.
 * @tparam T The arguments to the signal.
 */
template<typename Owner, typename... T>
using signal_ix_st = signal_interface<signal_g_st, Owner, int32_t, T...>;

template<typename Owner, GroupId Group, typename... T>
using signal_ix_g_st = signal_interface<signal_g_st, Owner, Group, T...>;

/**
 * @brief Specialization of signal_interface for multi-threaded signals.
 *
 * @tparam Owner The owner type. The call operator will only be accessible
 * from this type.
 * @tparam T The arguments to the signal.
 */
template<typename Owner, typename... T>
using signal_ix = signal_interface<signal_g, Owner, int32_t, T...>;

template<typename Owner, GroupId Group, typename... T>
using signal_ix_g = signal_interface<signal_g, Owner, Group, T...>;
} // namespace sigslot
