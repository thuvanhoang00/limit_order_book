# C++20 Order Book Practice Project

This repository provides the plumbing for a low-latency order-book exercise. The hot-path core is intentionally unfinished.

## Scope

Included:

- CMake + Ninja presets
- C++20 library skeleton
- GoogleTest contract tests
- Google Benchmark harness
- deterministic CSV event generator
- replay harness
- ASan/UBSan/TSan presets
- `perf stat`, `perf record`, and FlameGraph scripts
- CSV benchmark output

Not included:

- the internal order-book storage
- add/cancel/execute implementation
- optimized lookup, price ladder, allocator, or concurrency

Those are the parts you should implement and measure.

## Requirements

- Linux
- CMake 3.20+
- Ninja
- GCC or Clang with C++20 support
- Git when `ORDER_BOOK_FETCH_DEPS=ON`
- `perf` for profiling

GoogleTest and Google Benchmark are fetched automatically when they are not installed.

## Build

```bash
./tools/build.sh debug
./tools/build.sh release
```

Equivalent commands:

```bash
cmake --preset release
cmake --build --preset release --parallel
```

## Test workflow

```bash
./tools/run_tests.sh debug
```

Initially, only the scaffold and CSV I/O tests run. Core contract tests are prefixed with `DISABLED_`.

Implement in this order:

1. `AddOrder`
2. best bid/ask queries
3. duplicate detection
4. `CancelOrder`
5. partial/full `ExecuteOrder`
6. reset
7. invariant validation

Remove `DISABLED_` from one test at a time. Do not enable the whole suite before the previous behavior is correct.

## Suggested V1 data structures

Use a deliberately straightforward baseline:

```text
bid levels: std::map<Price, PriceLevel, std::greater<Price>>
ask levels: std::map<Price, PriceLevel, std::less<Price>>
order index: std::unordered_map<OrderId, OrderLocation>
per-level orders: stable FIFO container
```

The purpose of V1 is correctness and a measurable baseline, not minimal latency.

Define the invariants before optimizing:

- every live order exists exactly once in the order index
- every indexed order exists in exactly one price level
- no empty price level remains
- remaining quantity is positive
- best bid is the maximum bid price
- best ask is the minimum ask price
- order count equals the total number of live orders

## Generate workload

```bash
./build/release/generate_events data/events.csv 1000000 42
```

Arguments:

```text
generate_events <output.csv> <event_count> <seed>
```

The generator emits valid add/cancel/execute sequences and keeps active-order state internally.

## Replay

After implementing the core:

```bash
./tools/run.sh
```

By default, the script builds the `release` preset and replays
`data/sample_events.csv`. Pass a preset and CSV path to override them:

```bash
./tools/run.sh debug data/events.csv
```

Output includes:

- event count
- elapsed time
- ns/event
- million events/second
- remaining orders and level counts

The CSV parser is intentionally included in replay timing. Later, create a second benchmark that preloads decoded events to separate parsing cost from book-update cost.

## Benchmarks

```bash
./tools/run_benchmarks.sh release results
```

Output:

```text
results/benchmark.csv
```

The benchmark skips explicitly until the core no longer returns `NotImplemented`.

## Profiling

Generate a sufficiently large workload first:

```bash
./build/release/generate_events data/events.csv 5000000 42
```

Hardware counters:

```bash
./tools/perf_stat.sh
```

Sampling profile:

```bash
./tools/perf_record.sh
perf report -i results/perf.data
```

Flame graph:

```bash
export FLAMEGRAPH_DIR=/path/to/FlameGraph
./tools/flamegraph.sh
```

Track normalized metrics:

```text
ns/event
cycles/event
instructions/event
branch-misses/event
cache-misses/event
```

Do not optimize based only on `%CPU` or one flame graph.

## Sanitizers

```bash
./tools/run_tests.sh asan
./tools/build.sh tsan
```

TSan matters only after a concurrent pipeline is added. Keep the core single-writer first.

## Milestones

### V1: correctness baseline

- `std::map` levels
- hash lookup by order ID
- no concurrency
- all contract tests enabled
- invariant checker

### V2: measurement

- predecoded in-memory benchmark
- mixed add/cancel/execute benchmark
- fixed random seed
- p50/p95/p99 per-event latency sampling outside the hot loop
- `perf stat` and flame graph baseline

### V3: data-layout experiments

Compare one change at a time:

- node-based levels vs indexed price ladder
- heap allocation vs pool
- linked FIFO vs intrusive storage
- `std::unordered_map` vs flat/open-addressing lookup
- one event per call vs batch API

### V4: pipeline

Only after the single-threaded core is stable:

```text
file/network reader -> parser -> SPSC queue -> single-writer order book
```

Measure queue overhead, cache-line bouncing, backpressure, and scheduler interference.
