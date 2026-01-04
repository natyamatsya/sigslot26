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
   - Now includes safe reset tracking via `pending_weak_refs_` counter
   - `safe_reset()` waits for all weak references to clear before reuse
   - Benchmarks show 21% slower than DEFAULT for connect (156 ns vs 129 ns)
   - Not recommended for general use

3. **DEFAULT**: System allocator
   - Used when jemalloc/tcmalloc/mimalloc detected
   - Those allocators already have thread-local caches

**Benchmark Results** (AMD Ryzen 9 7950X3D, MSVC 19.50):

| Strategy | Connect Time | vs Lock-Free | Thread-Safety |
|----------|-------------|--------------|---------------|
| **DEFAULT** | **129 ns** | **baseline** ✅ | Lock-free CAS |
| ARENA (safe) | 156 ns | -21% slower | ✅ Safe reset tracking |

**Key Findings**:
- DEFAULT with lock-free CAS is optimal (129 ns)
- ARENA safe reset adds overhead from `pending_weak_refs_` tracking
- For larger allocation gains (20-50%), link with jemalloc/mimalloc
- Custom pools not worth the complexity vs specialized allocators

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
**Status**: ✅ **COMPLETE** (Dual-Counter Implementation)  
**Impact**: High  
**Effort**: High

**Problem**: `std::shared_ptr` has inherent overhead:
1. Separate control block allocation
2. Two atomic operations per copy (strong + weak count)
3. Virtual destructor indirection
4. `std::weak_ptr` anchor needed 16 extra bytes per slot

**Solution**: Dual-counter intrusive reference counting with native weak reference support.

```cpp
// Base class with embedded strong + weak reference counts
class intrusive_refcount {
    mutable std::atomic<std::size_t> m_strong{0};
    mutable std::atomic<std::size_t> m_weak{1};  // +1 while strong > 0
    
public:
    void add_ref() const noexcept {
        m_strong.fetch_add(1, std::memory_order_relaxed);
    }
    
    void release_ref() const noexcept {
        if (m_strong.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            destroy();           // Call destructor
            release_weak_ref();  // Release weak count held by strong refs
        }
    }
    
    // Lock-free CAS loop for weak_ptr::lock()
    [[nodiscard]] bool try_add_ref() const noexcept {
        std::size_t count = m_strong.load(std::memory_order_relaxed);
        while (count != 0) {
            if (m_strong.compare_exchange_weak(count, count + 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }
};
```

**Benchmark Results** (AMD Ryzen 9 7950X3D, MSVC 19.50):

| Metric | Phase 4 (std::weak_ptr anchor) | Phase 4 (dual-counter) | Improvement |
|--------|--------------------------------|------------------------|-------------|
| Connect Single Slot | 189 ns | 167 ns | **11% faster** |
| Emission Single Slot | 6.01 ns | 5.86 ns | **2% faster** |
| Memory per slot | ~56 bytes | ~40 bytes | **16 bytes saved** |

**Key Benefits**:
- ✅ Eliminated `std::weak_ptr` anchor overhead
- ✅ Lock-free `weak_ptr::lock()` via atomic CAS
- ✅ 16 bytes for counters fits in cache line with 3-slot SBO
- ✅ Foundation for future lock-free connect/disconnect

**Memory Layout**:
```
intrusive_refcount: 16 bytes (m_strong + m_weak atomics)
slot_state fields:  ~24 bytes (index, connected, blocked flags)
Total:              ~40 bytes (vs ~56 bytes with std::weak_ptr anchor)
```

**CMake Usage**:
```cmake
-DSIGSLOT_USE_INTRUSIVE_PTR=ON  # Enable dual-counter intrusive_ptr
```

#### 4.3 Connection Handle Optimization
**Status**: ✅ **Superseded** by 4.2 Dual-Counter  
**Impact**: Medium  
**Effort**: Medium

**Original Problem**: `connection` held `std::weak_ptr<slot_base>` requiring:
- 16 bytes storage
- Atomic operations to lock/check validity
- Control block must outlive all weak_ptrs

**Resolution**: The dual-counter `intrusive_weak_ptr` (Phase 4.2) provides:
- Lock-free `lock()` via CAS (same as proposed index-based approach)
- No separate control block (counters embedded in slot)
- Compatible with existing `connection` API (no breaking changes)

Index-based handles are no longer needed - `intrusive_weak_ptr` achieves the
same performance goals while maintaining API compatibility.

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

### Phase 5: Rx & Advanced Optimizations

#### 5.1 Batch Emission
**Status**: ✅ **COMPLETE**  
**Impact**: High (3.9x faster for sequential emissions)  
**Effort**: Low

For reactive pipelines emitting many values in sequence. Caches the slot snapshot
to avoid repeated atomic loads during burst emissions.

```cpp
// RAII batch emitter caches slot snapshot
{
    auto batch = sig.batch();
    for (int i = 0; i < 1000; ++i) {
        batch.emit(i);  // Uses cached snapshot
    }
}
```

**Implementation**:
- `batch_emitter` class holds `cow_copy_type<list_type>` snapshot
- `sig.batch()` returns RAII batch emitter
- `batch.emit()` / `batch()` emit using cached snapshot
- Snapshot is consistent for lifetime of batch_emitter

**Benchmark Results** (AMD Ryzen 9 7950X3D, MSVC 19.50):

| Emissions | Batch | Regular | Speedup |
|-----------|-------|---------|---------|
| 100 | 154 ns | 598 ns | **3.9x faster** |
| 1000 | 1514 ns | 5965 ns | **3.9x faster** |
| Throughput | **650 M/s** | 170 M/s | **3.8x higher** |

**Real-World Impact** (HFT Reactive Demo):
- Throughput: 1.19M → **1.33M ticks/sec** (+12%)
- Used for 100-tick burst emissions in exchange simulator

#### 5.2 Compile-Time Connections
**Status**: Future  
For static slot configurations known at compile time.

#### 5.3 SIMD Emission
Vectorized slot invocation for trivial callables.

#### 5.4 Lock-Free Connect/Disconnect
**Status**: ✅ **COMPLETE**  
**Impact**: High  
**Effort**: High

Full lock-free implementation using CAS loops. Built on dual-counter `intrusive_ptr`:

```cpp
// Lock-free add_slot using CAS loop
void add_slot(slot_ptr&& s) {
    while (true) {
        auto current = m_slots.read();
        auto new_groups = std::make_shared<list_type>(*current);
        // ... modify new_groups ...
        if (m_slots.try_publish(current, new_groups)) {
            break;  // Success!
        }
        // CAS failed - retry with fresh snapshot
    }
}
```

**Implemented**:
- `try_publish()` method in `rcu_cow` using `atomic_compare_exchange_strong`
- Lock-free `add_slot()`, `disconnect()`, `disconnect_all()`, `clean()`, `disconnect_if()`
- Removed `m_mutex` from `signal_base` for thread-safe signals
- Non-thread-safe signals use direct modification (no overhead)

**Benchmark Results** (AMD Ryzen 9 7950X3D, MSVC 19.50):

| Metric | Phase 4 (mutex) | Phase 5 (lock-free) | Improvement |
|--------|-----------------|---------------------|-------------|
| Connect Single Slot | 167 ns | 129 ns | **23% faster** |
| Thread-Safe Construction | 0.80 ns | 0.77 ns | **4% faster** |
| Thread-Safe Emission | 5.09 ns | 5.01 ns | **2% faster** |

---

## Why Not Hazard Pointers?

We evaluated hazard pointers (Folly, libcds, C++26 std::hazard_pointer) but
determined they are **overengineering** for sigslot's use case:

| Aspect | Hazard Pointers | RCU + intrusive_ptr |
|--------|-----------------|---------------------|
| **Reclamation** | Manual retire list | Automatic (dual-counter refcount) |
| **Global state** | Required (domain) | None |
| **Base class** | Required inheritance | `intrusive_refcount` (opt-in) |
| **Best for** | Intrusive linked structures | Swapped containers |

Our slot list is a `vector` that gets atomically swapped - RCU with dual-counter
`intrusive_ptr` is the natural fit. The embedded weak reference count provides
the same safety guarantees as hazard pointers for our use case.

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
