# Reactive Extensions Roadmap

This document outlines the implementation plan for additional reactive operators in `sigslot::rx`.

## Current State

Already implemented:
- `map(f)` - transform values
- `filter(pred)` - conditional forwarding
- `debounce(duration)` - suppress rapid emissions, emit after quiet period
- `observe_on(scheduler)` - switch execution context (stdexec)
- `debounce_on(scheduler, duration)` - scheduler-based debounce (stdexec)

---

## Phase 1: Throttle and Distinct

**Priority: High** - Most commonly requested rate-limiting operators.

### `throttle(duration)`
Emit the first value, then ignore subsequent values for the specified duration.

```cpp
auto throttled = sig | rx::throttle(100ms);
// sig emits: 1, 2, 3 (within 100ms), then waits 200ms, then 4, 5
// output:    1,                                        4
```

**Implementation:**
- Track `last_emission_time`
- Only forward if `now - last_emission_time >= duration`
- Reset timer on each forwarded emission

**Tests:**
- Throttle suppresses rapid emissions
- Throttle allows emissions after duration
- Throttle with zero emissions

### `distinct()`
Only emit when value differs from previous emission.

```cpp
auto distinct = sig | rx::distinct();
// sig emits: 1, 1, 2, 2, 2, 3, 1
// output:    1,    2,       3, 1
```

**Implementation:**
- Store `std::optional<T>` for previous value
- Compare with `==`, forward only if different
- Requires `T` to be equality comparable

**Tests:**
- Distinct filters consecutive duplicates
- Distinct allows non-consecutive duplicates
- Distinct with first emission

### `distinct_until_changed(pred)`
Like `distinct()` but with custom comparator.

```cpp
auto by_id = sig | rx::distinct_until_changed([](auto& a, auto& b) { 
    return a.id == b.id; 
});
```

---

## Phase 2: Scan and Buffer

**Priority: High** - Essential for state management and batching.

### `scan(init, accumulator)`
Running accumulator - emits accumulated value after each input.

```cpp
auto running_sum = sig | rx::scan(0, [](int acc, int x) { return acc + x; });
// sig emits: 1, 2, 3, 4
// output:    1, 3, 6, 10
```

**Implementation:**
- Store accumulator state
- Apply function, emit result, update state

**Tests:**
- Scan computes running sum
- Scan with different types (int -> string)
- Scan preserves state across emissions

### `buffer(count)`
Collect N emissions, then emit as `std::vector<T>`.

```cpp
auto batched = sig | rx::buffer(3);
// sig emits: 1, 2, 3, 4, 5, 6, 7
// output:    [1,2,3], [4,5,6]  (7 buffered, not emitted yet)
```

**Implementation:**
- Store `std::vector<T>` buffer
- When size reaches count, emit and clear

**Tests:**
- Buffer collects correct count
- Buffer handles partial final batch (flush on destruction?)
- Buffer with count=1 (passthrough)

### `buffer_time(duration)`
Collect emissions over time period, emit as vector.

```cpp
auto time_batched = sig | rx::buffer_time(100ms);
```

**Implementation:**
- Requires timer/scheduler
- Emit buffer contents when duration elapses

---

## Phase 3: Take, Skip, TakeUntil

**Priority: Medium** - Flow control operators.

### `take(count)`
Only forward first N emissions, then disconnect.

```cpp
auto first_five = sig | rx::take(5);
// sig emits: 1, 2, 3, 4, 5, 6, 7, 8
// output:    1, 2, 3, 4, 5
```

**Implementation:**
- Counter that decrements on each emission
- Disconnect when counter reaches 0

**Tests:**
- Take limits to N emissions
- Take with count=0 (no emissions)
- Take disconnects after limit

### `skip(count)`
Ignore first N emissions, forward the rest.

```cpp
auto after_warmup = sig | rx::skip(3);
// sig emits: 1, 2, 3, 4, 5
// output:          4, 5
```

**Implementation:**
- Counter that decrements until 0
- Forward only when counter is 0

**Tests:**
- Skip ignores first N
- Skip with count=0 (passthrough)
- Skip more than total emissions

### `take_until(signal)`
Forward emissions until another signal fires.

```cpp
sigslot::signal<> stop;
auto until_stop = sig | rx::take_until(stop);
// Forwards sig until stop() is called
```

**Implementation:**
- Connect to stop signal
- Disconnect from source when stop fires

**Tests:**
- TakeUntil stops on signal
- TakeUntil works if stop never fires
- TakeUntil cleanup on destruction

### `take_while(pred)`
Forward while predicate is true, stop on first false.

```cpp
auto while_positive = sig | rx::take_while([](int x) { return x > 0; });
```

---

## Phase 4: Multi-Signal Operators

**Priority: Medium** - Signal composition.

### `merge(sig1, sig2, ...)`
Combine multiple signals into one, emitting from any source.

```cpp
auto merged = rx::merge(sig1, sig2, sig3);
// Any emission from sig1, sig2, or sig3 is forwarded
```

**Implementation:**
- Connect to all source signals
- Forward all emissions to output

**Tests:**
- Merge forwards from all sources
- Merge handles different emission rates
- Merge cleanup when sources destroyed

### `combine_latest(sig1, sig2)`
Emit tuple of latest values when either signal fires.

```cpp
auto combined = rx::combine_latest(sig1, sig2);
// sig1 emits A, sig2 emits 1 -> (A, 1)
// sig1 emits B            -> (B, 1)
// sig2 emits 2            -> (B, 2)
```

**Implementation:**
- Store latest value from each signal
- Emit tuple when any fires (only after all have fired once)

**Tests:**
- CombineLatest waits for all signals
- CombineLatest emits on any change
- CombineLatest with different types

### `zip(sig1, sig2)`
Pair emissions 1:1, waiting for both.

```cpp
auto zipped = rx::zip(sig1, sig2);
// sig1 emits A, B, C
// sig2 emits 1, 2
// output: (A,1), (B,2)  // C waits for sig2
```

**Implementation:**
- Queue emissions from each signal
- Emit pair when both queues have items

**Tests:**
- Zip pairs correctly
- Zip handles unequal emission counts
- Zip with buffer limits

---

## Phase 5: Execution-Aware Variants

**Priority: Low** - stdexec integration for timing operators.

### `throttle_on(scheduler, duration)`
Throttle using scheduler timer instead of wall clock.

### `sample_on(scheduler, duration)`
Sample latest value at scheduler-driven intervals.

### `delay_on(scheduler, duration)`
Delay emissions using scheduler.

### `buffer_time_on(scheduler, duration)`
Time-based buffering with scheduler.

---

## Phase 6: Documentation and Examples

### Documentation
- Update README with reactive extensions section
- Add example usage for each operator
- Document thread-safety guarantees

### Examples
- `example/reactive-basic.cpp` - map, filter, distinct
- `example/reactive-timing.cpp` - throttle, debounce, sample
- `example/reactive-state.cpp` - scan, buffer
- `example/reactive-combine.cpp` - merge, zip, combine_latest

---

## Implementation Notes

### Type Traits
Extend `signal_traits` for each new wrapper type:
```cpp
template <typename Source>
struct signal_traits<throttled_signal<Source>> : signal_traits<Source> {};
```

### Testing Strategy
Each operator should have tests for:
1. Basic functionality
2. Edge cases (empty, single, many)
3. Connection management (disconnect, destruction)
4. Thread safety (if applicable)

### Pipe Operator Compatibility
Ensure all operators work with pipe syntax:
```cpp
auto result = sig 
    | rx::filter(pred) 
    | rx::throttle(100ms) 
    | rx::distinct();
```

---

## Timeline Estimate

| Phase | Operators | Complexity | Estimate |
|-------|-----------|------------|----------|
| 1 | throttle, distinct | Low | 1 session |
| 2 | scan, buffer | Medium | 1 session |
| 3 | take, skip, take_until | Low | 1 session |
| 4 | merge, combine_latest, zip | High | 2 sessions |
| 5 | Execution variants | Medium | 1 session |
| 6 | Docs & examples | Low | 1 session |

**Total: ~7 working sessions**
