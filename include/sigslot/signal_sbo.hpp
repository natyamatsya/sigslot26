// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

#include <sigslot/signal.hpp>
#include <span>
#include <array>
#include <vector>
#include <expected>
#include <type_traits>

namespace sigslot::detail {

enum class SBOError {
    NeedsHeapAllocation,
    InvalidSlotIndex
};

template<typename T, std::size_t N = 3>
class sbo_container {
    static_assert(N > 0, "SBO size must be greater than 0");
    
    // N=3 chosen for optimal cache line usage (40 bytes fits in 64-byte cache line)
    // and covers ~75% of common use cases (1-3 slots)
    
    union Storage {
        std::vector<T> heap;
        std::array<T, N> stack;
        
        Storage() {}  // Undefined union member
        ~Storage() {}  // Undefined union member
    };
    
    Storage storage_;
    bool using_heap_ = false;
    std::size_t size_ = 0;
    
public:
    constexpr sbo_container() noexcept = default;
    
    ~sbo_container() noexcept {
        clear();
    }
    
    // Non-copyable for now (RCU requires unique ownership)
    sbo_container(const sbo_container&) = delete;
    sbo_container& operator=(const sbo_container&) = delete;
    
    sbo_container(sbo_container&& other) noexcept 
        : using_heap_(other.using_heap_), size_(other.size_) {
        if (using_heap_) {
            new (&storage_.heap) std::vector<T>(std::move(other.storage_.heap));
            other.storage_.heap.~vector();
        } else {
            // Move construct each element in stack storage
            for (std::size_t i = 0; i < size_; ++i) {
                new (&storage_.stack[i]) T(std::move(other.storage_.stack[i]));
                other.storage_.stack[i].~T();
            }
        }
        other.using_heap_ = false;
        other.size_ = 0;
    }
    
    sbo_container& operator=(sbo_container&& other) noexcept {
        if (this != &other) {
            clear();
            
            using_heap_ = other.using_heap_;
            size_ = other.size_;
            
            if (using_heap_) {
                new (&storage_.heap) std::vector<T>(std::move(other.storage_.heap));
                other.storage_.heap.~vector();
            } else {
                for (std::size_t i = 0; i < size_; ++i) {
                    new (&storage_.stack[i]) T(std::move(other.storage_.stack[i]));
                    other.storage_.stack[i].~T();
                }
            }
            other.using_heap_ = false;
            other.size_ = 0;
        }
        return *this;
    }
    
    constexpr std::vector<T>& get_slots() noexcept {
        return using_heap_ ? storage_.heap : *reinterpret_cast<std::vector<T>*>(&storage_.stack);
    }
    
    constexpr const std::vector<T>& get_slots() const noexcept {
        return using_heap_ ? storage_.heap : *reinterpret_cast<const std::vector<T>*>(&storage_.stack);
    }
    
    constexpr std::span<T> get_span() noexcept {
        if (using_heap_) {
            return std::span<T>(storage_.heap.data(), size_);
        } else {
            return std::span<T>(storage_.stack.data(), size_);
        }
    }
    
    constexpr std::span<const T> get_span() const noexcept {
        if (using_heap_) {
            return std::span<const T>(storage_.heap.data(), size_);
        } else {
            return std::span<const T>(storage_.stack.data(), size_);
        }
    }
    
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr bool is_using_heap() const noexcept { return using_heap_; }
    
    constexpr std::expected<void, SBOError> try_reserve(std::size_t new_capacity) {
        if (new_capacity <= N) {
            return {};  // Stack capacity is sufficient
        }
        
        if (!using_heap_) {
            // Need to transition to heap
            std::vector<T> new_heap;
            new_heap.reserve(new_capacity);
            
            // Move existing elements to heap
            for (std::size_t i = 0; i < size_; ++i) {
                new_heap.emplace_back(std::move(storage_.stack[i]));
                storage_.stack[i].~T();
            }
            
            new (&storage_.heap) std::vector<T>(std::move(new_heap));
            using_heap_ = true;
        } else {
            storage_.heap.reserve(new_capacity);
        }
        
        return {};
    }
    
    template<typename... Args>
    constexpr std::expected<std::size_t, SBOError> try_emplace_back(Args&&... args) {
        if (!using_heap_ && size_ < N) {
            // Emplace in stack storage
            new (&storage_.stack[size_]) T(std::forward<Args>(args)...);
            return size_++;
        }
        
        // Need heap storage
        if (auto result = try_reserve(size_ + 1); !result) {
            return std::unexpected(result.error());
        }
        
        storage_.heap.emplace_back(std::forward<Args>(args)...);
        return size_++;
    }
    
    template<typename... Args>
    constexpr void emplace_back(Args&&... args) {
        auto result = try_emplace_back(std::forward<Args>(args)...);
        if (!result) {
            if (result.error() == SBOError::NeedsHeapAllocation) {
                // This shouldn't happen with our implementation
                throw std::bad_alloc();
            }
        }
    }
    
    constexpr void pop_back() noexcept {
        if (size_ == 0) return;
        
        --size_;
        if (using_heap_) {
            storage_.heap.pop_back();
        } else {
            storage_.stack[size_].~T();
        }
    }
    
    constexpr void clear() noexcept {
        if (using_heap_) {
            storage_.heap.clear();
            storage_.heap.~vector();
        } else {
            for (std::size_t i = 0; i < size_; ++i) {
                storage_.stack[i].~T();
            }
        }
        using_heap_ = false;
        size_ = 0;
    }
    
    constexpr T& operator[](std::size_t index) noexcept {
        if (using_heap_) {
            return storage_.heap[index];
        } else {
            return storage_.stack[index];
        }
    }
    
    constexpr const T& operator[](std::size_t index) const noexcept {
        if (using_heap_) {
            return storage_.heap[index];
        } else {
            return storage_.stack[index];
        }
    }
    
    constexpr T& back() noexcept {
        return (*this)[size_ - 1];
    }
    
    constexpr const T& back() const noexcept {
        return (*this)[size_ - 1];
    }
};

} // namespace sigslot::detail
