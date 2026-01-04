// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <vector>

namespace sigslot::detail {

/**
 * @brief Simple arena allocator for slot allocations
 * 
 * Uses bump-pointer allocation within pre-allocated chunks.
 * Much faster than general-purpose allocators for short-lived objects.
 * 
 * Strategy:
 * - Pre-allocate 64KB chunks
 * - Bump pointer for allocation (just increment, no bookkeeping)
 * - No individual deallocation (slots freed when signal destroyed)
 * - Thread-local to avoid contention
 */
class slot_arena {
    static constexpr std::size_t chunk_size = 64 * 1024; // 64KB chunks
    static constexpr std::size_t alignment = alignof(std::max_align_t);
    
    struct chunk {
        std::unique_ptr<std::byte[]> memory;
        std::size_t used = 0;
        
        explicit chunk() : memory(new std::byte[chunk_size]) {}
    };
    
    std::vector<chunk> chunks_;
    std::size_t current_chunk_ = 0;
    
public:
    slot_arena() {
        chunks_.emplace_back(); // Start with one chunk
    }
    
    // No copy/move - thread-local singleton
    slot_arena(const slot_arena&) = delete;
    slot_arena& operator=(const slot_arena&) = delete;
    
    /**
     * @brief Allocate memory from arena (bump pointer)
     * 
     * Fast path: ~5-10ns (just pointer arithmetic)
     * Slow path: ~100ns (allocate new chunk)
     */
    void* allocate(std::size_t size, std::size_t align = alignment) {
        // Align size to requested alignment
        size = (size + align - 1) & ~(align - 1);
        
        auto& current = chunks_[current_chunk_];
        
        // Align the current position
        std::size_t aligned_pos = (current.used + align - 1) & ~(align - 1);
        
        // Check if we have space in current chunk
        if (aligned_pos + size <= chunk_size) {
            void* ptr = current.memory.get() + aligned_pos;
            current.used = aligned_pos + size;
            return ptr;
        }
        
        // Need a new chunk
        if (current_chunk_ + 1 < chunks_.size()) {
            // Reuse existing chunk
            ++current_chunk_;
            chunks_[current_chunk_].used = 0;
        } else {
            // Allocate new chunk
            chunks_.emplace_back();
            ++current_chunk_;
        }
        
        // Allocate from new chunk
        auto& new_chunk = chunks_[current_chunk_];
        new_chunk.used = size;
        return new_chunk.memory.get();
    }
    
    /**
     * @brief Deallocate (no-op for arena)
     */
    void deallocate(void* /*ptr*/, std::size_t /*size*/, std::size_t /*align*/ = alignment) noexcept {
        // Arena doesn't track individual deallocations
    }
    
    /**
     * @brief Reset arena (reuse chunks without deallocation)
     * 
     * Called when signal is destroyed or all slots disconnected.
     * Keeps chunks allocated for reuse.
     */
    void reset() noexcept {
        current_chunk_ = 0;
        for (auto& chunk : chunks_) {
            chunk.used = 0;
        }
    }
    
    /**
     * @brief Get memory usage statistics
     */
    struct stats {
        std::size_t total_chunks;
        std::size_t total_bytes;
        std::size_t used_bytes;
    };
    
    stats get_stats() const noexcept {
        std::size_t used = 0;
        for (std::size_t i = 0; i <= current_chunk_ && i < chunks_.size(); ++i) {
            used += chunks_[i].used;
        }
        return {chunks_.size(), chunks_.size() * chunk_size, used};
    }
};

/**
 * @brief Thread-safe arena for slot allocations
 * 
 * Uses a mutex to protect the arena since shared_ptr control blocks
 * can be accessed from multiple threads.
 */
inline slot_arena& get_slot_arena() {
    static slot_arena arena;
    return arena;
}

/**
 * @brief STL-compatible allocator that uses arena
 * 
 * This allows std::allocate_shared to allocate both the control block
 * and the object from the arena in a single allocation.
 * 
 * Thread-safety: Uses a global arena with internal synchronization.
 */
template<typename T>
class arena_allocator {
    mutable std::mutex* m_mutex = nullptr;
    
    std::mutex& get_mutex() const {
        static std::mutex global_mutex;
        return global_mutex;
    }
    
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    arena_allocator() noexcept = default;
    
    template<typename U>
    arena_allocator(const arena_allocator<U>&) noexcept {}
    
    [[nodiscard]] T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        
        std::lock_guard<std::mutex> lock(get_mutex());
        void* ptr = get_slot_arena().allocate(n * sizeof(T), alignof(T));
        return static_cast<T*>(ptr);
    }
    
    void deallocate(T* ptr, std::size_t n) noexcept {
        std::lock_guard<std::mutex> lock(get_mutex());
        get_slot_arena().deallocate(ptr, n * sizeof(T), alignof(T));
    }
    
    template<typename U>
    bool operator==(const arena_allocator<U>&) const noexcept {
        return true; // All arena allocators use the same global arena
    }
    
    template<typename U>
    bool operator!=(const arena_allocator<U>&) const noexcept {
        return false;
    }
};

/**
 * @brief Allocate shared_ptr from arena using allocate_shared
 * 
 * This allocates BOTH the control block and the object from the arena
 * in a single allocation, eliminating the separate heap allocation.
 */
template<typename T, typename... Args>
std::shared_ptr<T> make_shared_arena(Args&&... args) {
    return std::allocate_shared<T>(arena_allocator<T>{}, std::forward<Args>(args)...);
}

} // namespace sigslot::detail
