# sigslot26 Performance Optimization Roadmap

## Current Architecture

The signal implementation uses a **copy-on-write (COW)** pattern with `std::shared_ptr`:

| Operation | Current Behavior | Lock Required |
|-----------|-----------------|---------------|
| **Emission** | Lock mutex, copy `shared_ptr`, unlock, iterate | Brief lock |
| **Connect** | Lock mutex, COW write, unlock | Full lock |
| **Disconnect** | Lock mutex, COW write, unlock | Full lock |

## Optimization Phases

### Phase 1: Quick Wins ✅ COMPLETE

#### 1.1 Relaxed Memory Ordering
**Status**: ✅ Implemented  
**Impact**: Low  

```cpp
// Optimized atomic loads for block flags
if (self.m_block.load(std::memory_order_relaxed)) { return; }
```

#### 1.2 Cache Line Alignment
**Status**: ✅ Implemented  
**Impact**: Medium (reduces false sharing under contention)  

```cpp
alignas(sigslot_cache_line_size) std::atomic<bool> m_block;
```

---

### Phase 2: Lock-Free Emission ✅ COMPLETE

#### 2.1 RCU-Style Atomic Shared Pointer
**Status**: ✅ Implemented  
**Impact**: High (lock-free read path)

Implemented using Read-Copy-Update (RCU) pattern with `std::atomic_load/store`:

```cpp
// Lock-free emission (readers)
auto slots = m_slots.read();  // atomic load, no mutex
for (auto& group : *slots) { ... }

// Writers still use mutex, but don't block readers
{
    lock_guard lock(m_mutex);
    auto guard = cow_write(m_slots);  // copy-on-write
    guard.get().push_back(...);
}   // publishes atomically on destruction
```

**Key insight**: RCU is the correct pattern for pointer-containing data structures.
Hazard pointers would be overkill - `shared_ptr` reference counting provides
automatic deferred reclamation.

---

### Phase 3: Future Micro-Optimizations

#### 3.1 Small Buffer Optimization (SBO)
**Status**: ✅ **COMPLETE**  
**Impact**: Medium  
**Effort**: Medium

For signals with ≤3 slots, avoid heap allocation entirely.

**Implementation Details**:
- **Standard Library Approach**: Uses flag bits in size field (like `std::string`)
- **Type Safety**: Proper `std::aligned_storage_t` instead of type punning
- **Compact Layout**: Eliminates separate `bool` flag, uses high bit of size
- **Vector Compatibility**: Full `std::vector` interface for seamless integration
- **Iterator Support**: Standard-compliant random access iterators

**Key Features**:
```cpp
// Flag-bit optimization like std::string
static constexpr std::size_t heap_flag = std::size_t{1} << (sizeof(std::size_t) * 8 - 1);
std::size_t size_and_flag_;  // High bit = heap, low bits = size

// Proper union management
union Storage {
    std::vector<T> heap;
    std::aligned_storage_t<sizeof(T), alignof(T)> buffer[N];
};
```

**Performance Benefits**:
- Zero heap allocation for ~75% of use cases (1-3 slots)
- Cache-line optimized (fits in 64-byte cache line)
- Standard library compatibility
- Memory overhead: 16 bytes vs 24-32 bytes for heap

**Test Coverage**: 8 comprehensive test suites (128-138 tests)

---

### Phase 4: Connection Path Optimization

The Phase 2 RCU implementation improved emission latency by 35% but introduced
regression in the connection path (64.6 ns → 160 ns baseline). Phase 4 focuses
on recovering this overhead.

#### 4.1 Slot Memory Pool
**Status**: ✅ Implemented  
**Impact**: Low-Medium (3% improvement with PMR)  
**Effort**: Medium

**Problem**: Each `connect()` calls `std::make_shared<slot_t>()` which:
1. Allocates control block (~32 bytes)
2. Allocates slot object (~48-64 bytes depending on callable)
3. Two separate allocations = poor cache locality

**Solution**: Thread-local memory pool with auto-detection of fast allocators.

**Implementation Strategies**:

1. **PMR (Recommended)**: `std::pmr::unsynchronized_pool_resource`
   - Thread-local pools (no contention)
   - Standard C++17
   - 3% performance improvement

2. **ARENA (Experimental)**: Custom bump-pointer allocator
   - Single allocation for control block + object
   - Thread-safety requires global mutex (adds contention)
   - Not recommended for production

3. **DEFAULT**: System allocator
   - Used when jemalloc/tcmalloc/mimalloc detected
   - Those allocators already have thread-local caches

**Benchmark Results** (AMD Ryzen 9 7950X3D, MSVC 19.50):

| Strategy | Connect Time | vs Baseline | Thread-Safety |
|----------|-------------|-------------|---------------|
| **PMR** | **167 ns** | **3% faster** ✅ | Thread-local pools |
| ARENA (TLS) | 157 ns | 9% faster | ❌ Crashes (cross-thread `shared_ptr`) |
| ARENA (mutex) | 174 ns | -1% slower | ✅ Global mutex contention |
| DEFAULT | 172 ns | baseline | ✅ MSVC allocator |

**Key Findings**:
- PMR provides best balance of performance and safety
- MSVC's default allocator is already well-optimized
- Arena allocator incompatible with `shared_ptr` threading model
- For larger gains (20-50%), link with jemalloc/mimalloc

**CMake Usage**:
```cmake
# Auto-detect (default): ARENA if no fast allocator, else DEFAULT
-DSIGSLOT_SLOT_ALLOCATOR=AUTO

# Manual override
-DSIGSLOT_SLOT_ALLOCATOR=DEFAULT  # System allocator
-DSIGSLOT_SLOT_ALLOCATOR=PMR      # std::pmr pool (recommended)
-DSIGSLOT_SLOT_ALLOCATOR=ARENA    # Custom arena (experimental)
```

**Code Example**:
```cpp
#if SIGSLOT_USE_SLOT_POOL == 1
// PMR strategy
template<typename B, typename D, typename... Arg>
inline std::shared_ptr<B> make_shared(Arg&&... arg) {
    std::pmr::polymorphic_allocator<D> alloc(get_slot_pool());
    return std::allocate_shared<D>(alloc, std::forward<Arg>(arg)...);
}
#endif
```

#### 4.2 Intrusive Reference Counting
**Status**: Research  
**Impact**: High  
**Effort**: High

**Problem**: `std::shared_ptr` has inherent overhead:
1. Separate control block allocation
2. Two atomic operations per copy (strong + weak count)
3. Virtual destructor indirection

**Solution**: Intrusive reference counting eliminates control block.

```cpp
// Base class with embedded reference count
class intrusive_slot_base {
    mutable std::atomic<std::size_t> m_refcount{1};
    
public:
    void add_ref() const noexcept {
        m_refcount.fetch_add(1, std::memory_order_relaxed);
    }
    
    void release() const noexcept {
        if (m_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
};

// Smart pointer that works with intrusive types
template<typename T>
class intrusive_ptr {
    T* m_ptr = nullptr;
public:
    // ~8 bytes vs 16 bytes for shared_ptr
    // No control block allocation
    // Single atomic per copy
};
```

**Expected Impact**: 
- Reduce slot memory from ~80 bytes to ~48 bytes
- Faster copy operations (single atomic vs two)
- Better cache locality

**Trade-offs**:
- Requires base class modification (breaking change for custom slots)
- No weak_ptr equivalent (connection handles need redesign)
- More complex implementation

#### 4.3 Connection Handle Optimization
**Status**: Proposed  
**Impact**: Medium  
**Effort**: Medium

**Problem**: Current `connection` holds `std::weak_ptr<slot_base>`:
- 16 bytes storage
- Requires atomic operations to lock/check validity
- Control block must outlive all weak_ptrs

**Solution**: Index-based connection handles.

```cpp
// Lightweight connection handle
class connection {
    signal_base* m_signal;      // 8 bytes
    std::uint32_t m_slot_id;    // 4 bytes (unique ID, not index)
    std::uint32_t m_generation; // 4 bytes (ABA protection)
    // Total: 16 bytes, no allocation, no atomics for storage
    
public:
    bool connected() const {
        return m_signal && m_signal->has_slot(m_slot_id, m_generation);
    }
    
    void disconnect() {
        if (m_signal) m_signal->remove_slot(m_slot_id);
    }
};
```

**Expected Impact**:
- Zero allocation for connection objects
- Faster validity checks (no weak_ptr lock)
- Smaller memory footprint

**Trade-offs**:
- Requires slot ID management in signal
- Generation counter needed for ABA safety
- Breaking change for connection API

#### 4.4 Lazy Slot Cleanup
**Status**: ❌ **Blocked** (API incompatibility)  
**Impact**: Medium  
**Effort**: Low

**Problem**: Disconnection triggers immediate COW + cleanup:
1. Lock mutex
2. Copy entire slot vector
3. Remove slot
4. Publish atomically

**Attempted Solution**: Mark slots as "tombstoned" and batch cleanup during connect().

**Finding**: This approach is **incompatible with the current API**:
- `connection::valid()` checks if `weak_ptr` is still valid
- With lazy cleanup, the slot stays in the container (just marked disconnected)
- This keeps the `weak_ptr` valid, breaking `IsDisconnected()` checks
- Tests like `REQUIRE_THAT(conn, IsDisconnected())` fail

**Resolution Options**:
1. **Breaking change**: Redefine `valid()` to check `connected()` instead of weak_ptr validity
2. **Alternative design**: Keep immediate removal but optimize the COW path
3. **Defer**: Focus on other optimizations (4.1 Slot Memory Pool) first

**Recommendation**: Pursue 4.1 (Slot Memory Pool) instead - it's non-breaking and addresses
the larger connect regression.

---

### Phase 5: Rx & Advanced Optimizations (Future)

#### 5.1 Batch Emission
**Status**: Design phase  
**Impact**: Low-Medium  
**Effort**: Low

For reactive pipelines emitting many values in sequence. Optimize `rx::` operators
that emit multiple values rapidly.

#### 5.2 Compile-Time Connections
For static slot configurations known at compile time.

#### 5.3 SIMD Emission
Vectorized slot invocation for trivial callables.

#### 5.4 Lock-Free Connect/Disconnect
Full lock-free implementation using CAS loops.

---

## Why Not Hazard Pointers?

We evaluated hazard pointers (Folly, libcds, C++26 std::hazard_pointer) but
determined they are **overengineering** for sigslot's use case:

| Aspect | Hazard Pointers | RCU + shared_ptr |
|--------|-----------------|------------------|
| **Reclamation** | Manual retire list | Automatic (refcount) |
| **Global state** | Required (domain) | None |
| **Base class** | Required inheritance | None |
| **Best for** | Intrusive linked structures | Swapped containers |

Our slot list is a `vector` that gets atomically swapped - RCU is the natural fit.

---

## Benchmark Targets

1. **Emission latency** (single-threaded, multi-threaded)
2. **Connect/disconnect throughput**
3. **Contention scaling** (1, 2, 4, 8, 16 threads)
4. **Memory usage** per signal/slot

---

## References

- [Read-Copy-Update (Wikipedia)](https://en.wikipedia.org/wiki/Read-copy-update)
- [std::atomic_load/store for shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic)
- [std::hardware_destructive_interference_size](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
- [std::memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)
