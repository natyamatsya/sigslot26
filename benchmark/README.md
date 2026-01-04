# Sigslot Benchmarks

This directory contains Google Benchmark tests for sigslot performance analysis.

## Building Benchmarks

```bash
# Configure with benchmark support
cmake -B build -DSIGSLOT_ENABLE_BENCHMARK=ON

# Build benchmarks
cmake --build build --target signal_benchmarks threaded_benchmarks

# Run benchmarks
./build/benchmark/benchmarks/signal_benchmarks
./build/benchmark/benchmarks/threaded_benchmarks
```

## Benchmark Categories

### Single-threaded Benchmarks (`signal_benchmarks`)
- **Signal Construction/Destruction**: Overhead of creating/destroying signals
- **Connect/Disconnect**: Performance of slot management
- **Emission**: Signal emission latency with 1 and 10 slots
- **Slot Count**: Performance of slot_count() queries

### Multi-threaded Benchmarks (`threaded_benchmarks`)
- **Thread-safe Construction**: Overhead of thread-safe signals
- **Thread-safe Emission**: Performance with atomic operations
- **Concurrent Emission**: Multiple threads emitting simultaneously
- **Concurrent Connect**: Multiple threads connecting simultaneously

## Running Benchmarks

### Basic Run
```bash
./signal_benchmarks
```

### Custom Options
```bash
# Filter specific benchmarks
./signal_benchmarks --benchmark_filter="Emission"

# Run for minimum time
./signal_benchmarks --benchmark_min_time=5

# Output to JSON
./signal_benchmarks --benchmark_out=results.json
```

## Performance Targets

These benchmarks establish baselines for optimization phases:

- **Phase 1**: Relaxed memory ordering + cache alignment
- **Phase 2**: Lock-free emission via RCU pattern
- **Phase 3**: Small buffer optimization (3-slot SBO)
- **Phase 4**: Dual-counter intrusive_ptr (lock-free weak references)

## SBO Layout Analysis

Run the `analyze-sbo-layout` tool to see actual type sizes:

```bash
cmake --build build --target analyze-sbo-layout
./build/benchmark/analysis/analyze-sbo-layout
```

With dual-counter intrusive_ptr enabled (`-DSIGSLOT_USE_INTRUSIVE_PTR=ON`):
- `intrusive_ptr<T>`: 8 bytes (vs 16 bytes for `shared_ptr`)
- `intrusive_weak_ptr<T>`: 8 bytes (vs 16 bytes for `weak_ptr`)
- 3-slot SBO fits in 64-byte cache line: **40 bytes total**

## Interpreting Results

- **Time/Iteration**: Lower is better
- **Items/Second**: Higher is better
- **Standard Deviation**: Lower variance = more consistent performance
