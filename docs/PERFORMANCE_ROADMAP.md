# sigslot26 Performance Optimization Roadmap

## Current Architecture

The signal implementation uses a **copy-on-write (COW)** pattern with `std::shared_ptr`:

| Operation | Current Behavior | Lock Required |
|-----------|-----------------|---------------|
| **Emission** | Lock mutex, copy `shared_ptr`, unlock, iterate | Brief lock |
| **Connect** | Lock mutex, COW write, unlock | Full lock |
| **Disconnect** | Lock mutex, COW write, unlock | Full lock |
| **slot_count()** | Lock mutex, copy `shared_ptr` | Unnecessary |

## Optimization Roadmap

### Phase 1: Quick Wins (C++23)

#### 1.1 Relaxed Memory Ordering
**Status**: Ready to implement  
**Impact**: Low  
**Effort**: Trivial

```cpp
// Current
if (self.m_block) { return; }

// Optimized
if (self.m_block.load(std::memory_order_relaxed)) { return; }
```

#### 1.2 Cache Line Alignment
**Status**: Ready to implement  
**Impact**: Medium (2-5x under contention)  
**Effort**: Low

```cpp
struct alignas(std::hardware_destructive_interference_size) signal_base {
    std::atomic<bool> m_block;
    // hot path data grouped together
};
```

---

### Phase 2: Lock-Free Emission (C++20)

#### 2.1 Atomic Shared Pointer for Slot List
**Status**: Requires design work  
**Impact**: High (10-50x under contention)  
**Effort**: Medium

Replace `copy_on_write<T>` + mutex with `std::atomic<std::shared_ptr<T>>`:

```cpp
// Current: requires mutex
cow_copy_type<list_type> ref = slots_reference();  // locks mutex

// Lock-free: atomic load
std::atomic<std::shared_ptr<list_type>> m_slots;
auto ref = m_slots.load(std::memory_order_acquire);
```

**Considerations**:
- `std::atomic<std::shared_ptr<T>>` may not be lock-free on all platforms
- Use `std::atomic<std::shared_ptr<T>>::is_always_lock_free` to check
- Fallback to mutex-based implementation when not lock-free

---

### Phase 3: Advanced Lock-Free (C++26 / Optional Dependencies)

#### 3.1 Hazard Pointer Integration

**Available Implementations**:

| Library | License | Header-Only | Production Ready | Notes |
|---------|---------|-------------|------------------|-------|
| **Facebook Folly** | Apache-2.0 | No | ✅ Yes | Reference impl for C++26, heavy use at Meta |
| **libcds** | BSL-1.0 | Mostly | ✅ Yes | Full suite of lock-free structures |
| **parlay::hazard_pointers** | MIT | ✅ Yes | Experimental | Custom impl for atomic_shared_ptr |
| **std::hazard_pointer** | N/A | ✅ Yes | C++26 only | Standard library (future) |

**Recommended Approach**:
1. Design interface compatible with `std::hazard_pointer` (C++26)
2. Provide optional adapter for Folly or libcds for pre-C++26
3. Auto-detect C++26 and use standard implementation when available

```cpp
#if __cpp_lib_hazard_pointer >= 202306L
    using hazard_pointer = std::hazard_pointer;
#elif defined(SIGSLOT_USE_FOLLY)
    using hazard_pointer = folly::hazard_pointer;
#elif defined(SIGSLOT_USE_LIBCDS)
    using hazard_pointer = cds::gc::HP::hazard_pointer;
#endif
```

#### 3.2 Lock-Free Connect/Disconnect
**Status**: Future consideration  
**Impact**: Very High  
**Effort**: High

With hazard pointers, both emission AND mutation can be lock-free:
- Emission: Protected read with hazard pointer
- Connect: Lock-free append with CAS
- Disconnect: Lock-free removal with hazard pointer protection

---

### Phase 4: Micro-Optimizations

#### 4.1 Small Buffer Optimization (SBO)
**Status**: Design phase  
**Impact**: Medium  
**Effort**: Medium

For signals with ≤3 slots, avoid heap allocation entirely.

#### 4.2 Batch Emission
**Status**: Design phase  
**Impact**: Low-Medium  
**Effort**: Low

For reactive pipelines emitting many values in sequence.

---

## Recommended Strategy

### Approach: Future-Proof API with Optional Backends

1. **Design API compatible with `std::hazard_pointer`** (C++26)
   - Use the standard interface as the target abstraction
   - This ensures zero migration effort when C++26 is adopted

2. **Provide optional adapters for pre-C++26**
   - **Facebook Folly** (recommended for production): Battle-tested at Meta scale
   - **libcds** (alternative): Full lock-free container suite, BSL-1.0 license

3. **Auto-detect and prefer standard library**
   - When `__cpp_lib_hazard_pointer` is defined, use `std::hazard_pointer`
   - Otherwise, fall back to configured optional dependency
   - If no dependency configured, use current mutex-based implementation

### Implementation Order

```
Phase 1 (Now)         Phase 2 (C++20)           Phase 3 (C++26/Optional)
─────────────────     ─────────────────────     ────────────────────────
• Relaxed ordering    • atomic<shared_ptr>      • Hazard pointer backend
• Cache alignment     • Lock-free emission      • Lock-free mutation
• Quick wins          • Major perf gain         • Full lock-free
```

### Why This Order?

- **Phase 1** requires no API changes and can ship immediately
- **Phase 2** provides the biggest performance win with minimal risk
- **Phase 3** is optional - users who need maximum performance can opt-in

---

## Optional Dependencies Summary

| Dependency | Purpose | Integration |
|------------|---------|-------------|
| **Facebook Folly** | Hazard pointers, F14FastSet | CMake `find_package(folly)` |
| **libcds** | Hazard pointers, lock-free containers | CMake `find_package(libcds)` |
| **ParlayLib** | Hazard pointers (experimental) | Header-only, git submodule |

### CMake Integration Pattern

```cmake
option(SIGSLOT_USE_FOLLY "Use Facebook Folly for hazard pointers" OFF)
option(SIGSLOT_USE_LIBCDS "Use libcds for hazard pointers" OFF)

if(SIGSLOT_USE_FOLLY)
    find_package(folly REQUIRED)
    target_link_libraries(sigslot INTERFACE Folly::folly)
    target_compile_definitions(sigslot INTERFACE SIGSLOT_HAVE_FOLLY)
elseif(SIGSLOT_USE_LIBCDS)
    find_package(libcds REQUIRED)
    target_link_libraries(sigslot INTERFACE libcds::cds)
    target_compile_definitions(sigslot INTERFACE SIGSLOT_HAVE_LIBCDS)
endif()
```

---

## Benchmark Targets

Before implementing optimizations, establish baseline benchmarks:

1. **Emission latency** (single-threaded, multi-threaded)
2. **Connect/disconnect throughput**
3. **Contention scaling** (1, 2, 4, 8, 16 threads)
4. **Memory usage** per signal/slot

---

## References

- [P2530R3: Why Hazard Pointers Should Be in C++26](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2530r3.pdf)
- [CppCon 2023: Lock-free Atomic Shared Pointers](https://www.youtube.com/watch?v=lNPZV9Iqo3U)
- [Facebook Folly Hazard Pointers](https://github.com/facebook/folly/blob/main/folly/synchronization/Hazptr.h)
- [libcds Documentation](https://libcds.sourceforge.net/doc/cds-api/index.html)
