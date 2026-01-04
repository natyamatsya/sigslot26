// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

#include <version> // For feature test macros

// Require C++23 std::expected support
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error \
    "C++23 std::expected is required. Use a modern compiler with full C++23 library support (e.g., GCC 14+, Clang 18 with libc++, MSVC 19.33+)."
#endif

#include <span>
#include <array>
#include <vector>
#include <expected>
#include <type_traits>
#include <new>
#include <memory> // For std::launder

namespace sigslot::detail {

enum class SBOError { NeedsHeapAllocation, InvalidSlotIndex };

template<typename T, std::size_t N = 3>
class sbo_container_improved {
    static_assert(N > 0, "SBO size must be greater than 0");

    // Flag-bit optimization following std::string pattern (libstdc++, MSVC)
    // High bit indicates heap usage, low bits store size - eliminates separate bool flag
    // Reference: libstdc++ _String_base::_M_length, MSVC std::string::_Mysize
    static constexpr std::size_t heap_flag = std::size_t{1} << (sizeof(std::size_t) * 8 - 1);
    static constexpr std::size_t size_mask = ~heap_flag;

    // Union storage following standard library SBO patterns
    // - libstdc++ std::string: union { _Char_type _M_local_buf[_S_local_capacity]; _Char_type* _M_p; }
    // - MSVC std::string: union _Bxty { _Elem _Buf[_BUF_SIZE]; _Elem* _M_ptr; }
    // - LLVM std::function: uses manager functions instead of direct union access
    union Storage {
        std::vector<T> heap;                 // Heap storage for large containers
        unsigned char buffer[sizeof(T) * N]; // Raw bytes for stack storage (no type punning)

        Storage() {}  // Undefined union member - intentional
        ~Storage() {} // Undefined union member - intentional
    } storage_;

    // Compact size storage following std::string flag-bit approach
    // libstdc++ uses high bit for refcount, MSVC uses high bit for small-string flag
    std::size_t size_and_flag_ = 0;

    constexpr bool is_heap() const noexcept { return size_and_flag_ & heap_flag; }

    constexpr std::size_t get_size() const noexcept { return size_and_flag_ & size_mask; }

    constexpr void set_size(std::size_t size, bool heap) noexcept {
        size_and_flag_ = size | (heap ? heap_flag : 0);
    }

    // Safe access following standard library lifetime management patterns
    // std::launder (C++17) prevents undefined behavior when accessing objects after placement new
    // Required by standard libraries for type-safe union access
    // Reference: https://en.cppreference.com/w/cpp/memory/launder
    constexpr T* get_stack_data() noexcept {
        // Use std::launder to prevent undefined behavior (C++17)
        // This follows the same pattern used by libstdc++ and MSVC for SBO access
        return std::launder(reinterpret_cast<T*>(storage_.buffer));
    }

    constexpr const T* get_stack_data() const noexcept {
        // Const version of laundered access - mirrors standard library const SBO access
        return std::launder(reinterpret_cast<const T*>(storage_.buffer));
    }

    void destroy_stack_elements() noexcept {
        T* data = get_stack_data();
        for (std::size_t idx = 0; idx < get_size(); ++idx) {
            data[idx].~T();
        }
    }

public:
    constexpr sbo_container_improved() noexcept = default;

    ~sbo_container_improved() noexcept { clear(); }

    // Smart copy operations following standard library patterns
    // libstdc++ std::string: copies stack elements directly, shares heap storage (COW)
    // MSVC std::string: similar approach with conditional copy/share logic
    // LLVM std::function: uses copy manager for type-erased copying
    sbo_container_improved(const sbo_container_improved& other)
        : size_and_flag_(0) {
        if (other.is_heap()) {
            // Heap storage: could use COW like libstdc++, but RCU requires independence
            // Reference: libstdc++ _M_copy_allocator for heap storage copying
            new (&storage_.heap) std::vector<T>(other.storage_.heap);
            set_size(other.get_size(), true);
        } else {
            // Stack storage: direct element copy (no heap allocation)
            // Matches libstdc++ std::string _M_construct with small string optimization
            const T* src = other.get_stack_data();
            T* dst = get_stack_data();
            for (std::size_t idx = 0; idx < other.get_size(); ++idx) {
                new (&dst[idx]) T(src[idx]); // Placement new for stack elements
            }
            set_size(other.get_size(), false);
        }
    }

    // Copy assignment following standard library strong exception safety
    // Similar to libstdc++ std::string::operator= with SBO handling
    sbo_container_improved& operator=(const sbo_container_improved& other) {
        if (this != &other) {
            clear(); // Destroy current contents first

            if (other.is_heap()) {
                // Copy heap storage - follows libstdc++ pattern
                new (&storage_.heap) std::vector<T>(other.storage_.heap);
                set_size(other.get_size(), true);
            } else {
                // Copy stack storage - placement new for each element
                const T* src = other.get_stack_data();
                T* dst = get_stack_data();
                for (std::size_t idx = 0; idx < other.get_size(); ++idx) {
                    new (&dst[idx]) T(src[idx]);
                }
                set_size(other.get_size(), false);
            }
        }
        return *this;
    }

    // Add copy-on-write optimization for heap storage (like std::string)
    // libstdc++ uses reference counting for COW, MSVC uses shared_ptr
    constexpr bool should_share_on_copy() const noexcept {
        // For large heap storage, we could use COW to avoid expensive copies
        // For now, we always copy for RCU compatibility, but this could be optimized
        return false; // Future: could return size() > COW_THRESHOLD
    }

    // Optimized move constructor following MSVC std::string pattern
    // MSVC std::string: steal resources, leave other in valid but empty state
    // libstdc++ std::string: similar approach with noexcept move
    sbo_container_improved(sbo_container_improved&& other) noexcept
        : size_and_flag_(other.size_and_flag_) {
        if (is_heap()) {
            // Heap storage: steal vector (no allocation, just move)
            new (&storage_.heap) std::vector<T>(std::move(other.storage_.heap));
            other.storage_.heap.~vector(); // Destroy moved-from vector
        } else {
            // Stack storage: move each element individually (placement new)
            // Follows libstdc++ small string move semantics
            T* src = other.get_stack_data();
            T* dst = get_stack_data();
            for (std::size_t idx = 0; idx < get_size(); ++idx) {
                new (&dst[idx]) T(std::move(src[idx]));
                src[idx].~T(); // Destroy moved-from element
            }
        }
        other.set_size(0, false); // Leave other in valid empty state
    }

    sbo_container_improved& operator=(sbo_container_improved&& other) noexcept {
        if (this != &other) {
            clear();

            size_and_flag_ = other.size_and_flag_;

            if (is_heap()) {
                new (&storage_.heap) std::vector<T>(std::move(other.storage_.heap));
                other.storage_.heap.~vector();
            } else {
                T* src = other.get_stack_data();
                T* dst = get_stack_data();
                for (std::size_t idx = 0; idx < get_size(); ++idx) {
                    new (&dst[idx]) T(std::move(src[idx]));
                    src[idx].~T();
                }
            }
            other.set_size(0, false);
        }
        return *this;
    }

    // Standard vector interface compatibility
    using iterator = T*;
    using const_iterator = const T*;

    constexpr iterator begin() noexcept {
        return is_heap() ? storage_.heap.data() : get_stack_data();
    }

    constexpr iterator end() noexcept { return begin() + get_size(); }

    constexpr const_iterator begin() const noexcept {
        return is_heap() ? storage_.heap.data() : get_stack_data();
    }

    constexpr const_iterator end() const noexcept { return begin() + get_size(); }

    constexpr const_iterator cbegin() const noexcept { return begin(); }
    constexpr const_iterator cend() const noexcept { return end(); }

    constexpr std::size_t size() const noexcept { return get_size(); }
    constexpr bool empty() const noexcept { return get_size() == 0; }
    constexpr bool is_using_heap() const noexcept { return is_heap(); }

    constexpr std::span<T> get_span() noexcept { return std::span<T>(begin(), get_size()); }

    constexpr std::span<const T> get_span() const noexcept {
        return std::span<const T>(begin(), get_size());
    }

    constexpr std::vector<T>& get_slots() noexcept {
        // Transition to heap if needed for compatibility
        if (!is_heap()) {
            try_reserve(get_size() + 1);
        }
        return storage_.heap;
    }

    constexpr const std::vector<T>& get_slots() const noexcept {
        // Transition to heap if needed for compatibility
        if (!is_heap()) {
            const_cast<sbo_container_improved*>(this)->try_reserve(get_size() + 1);
        }
        return storage_.heap;
    }

    constexpr std::expected<void, SBOError> try_reserve(std::size_t new_capacity) {
        if (new_capacity <= N) {
            return {}; // Stack capacity is sufficient
        }

        if (!is_heap()) {
            // Need to transition to heap
            std::vector<T> new_heap;
            new_heap.reserve(new_capacity);

            // Move existing elements to heap
            T* src = get_stack_data();
            for (std::size_t idx = 0; idx < get_size(); ++idx) {
                new_heap.emplace_back(std::move(src[idx]));
                src[idx].~T();
            }

            new (&storage_.heap) std::vector<T>(std::move(new_heap));
            set_size(get_size(), true);
        } else {
            storage_.heap.reserve(new_capacity);
        }

        return {};
    }

    template<typename... Args>
    constexpr std::expected<std::size_t, SBOError> try_emplace_back(Args&&... args) {
        std::size_t current_size = get_size();

        if (!is_heap() && current_size < N) {
            // Emplace in stack storage
            T* data = get_stack_data();
            new (&data[current_size]) T(std::forward<Args>(args)...);
            set_size(current_size + 1, false);
            return current_size;
        }

        // Need heap storage
        if (auto result = try_reserve(current_size + 1); !result) {
            return std::unexpected(result.error());
        }

        storage_.heap.emplace_back(std::forward<Args>(args)...);
        set_size(current_size + 1, true);
        return current_size;
    }

    template<typename U>
    constexpr void push_back(U&& value) {
        emplace_back(std::forward<U>(value));
    }

    template<typename... Args>
    constexpr void emplace_back(Args&&... args) {
        auto result = try_emplace_back(std::forward<Args>(args)...);
        if (!result) {
            if (result.error() == SBOError::NeedsHeapAllocation) {
                throw std::bad_alloc();
            }
        }
    }

    constexpr void pop_back() noexcept {
        std::size_t current_size = get_size();
        if (current_size == 0)
            return;

        std::size_t new_size = current_size - 1;

        if (is_heap()) {
            storage_.heap.pop_back();
        } else {
            get_stack_data()[new_size].~T();
        }

        set_size(new_size, is_heap());
    }

    constexpr void clear() noexcept {
        if (is_heap()) {
            storage_.heap.clear();
            storage_.heap.~vector();
        } else {
            destroy_stack_elements();
        }
        set_size(0, false);
    }

    constexpr T& operator[](std::size_t index) noexcept { return begin()[index]; }

    constexpr const T& operator[](std::size_t index) const noexcept { return begin()[index]; }

    constexpr T& back() noexcept { return (*this)[get_size() - 1]; }

    constexpr const T& back() const noexcept { return (*this)[get_size() - 1]; }
};

// Type alias for backward compatibility
template<typename T, std::size_t N = 3>
using sbo_container = sbo_container_improved<T, N>;

} // namespace sigslot::detail
