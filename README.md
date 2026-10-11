# C++20 Order Book Practice Project

A single-writer order-book baseline with implemented add, cancel, partial/full
execution, best bid/ask queries, reset, and invariant validation. The project also
includes CSV generation/replay, contract tests, profiling tools, and standalone
memory-pool experiments.

## Current scope

The book uses ordered `std::map` price levels, a `std::unordered_map` order index,
and per-level `std::list` order queues. Core contract tests are enabled, including
mixed workloads checked against a reference model.

Each book owns a fixed-slot pool shared by its order queues; the price-level maps
and order index still allocate on the heap. The pool outlives all queues.
`apply(AddOrder)` remains `noexcept`: allocation failure returns
`ApplyResult::AllocationFailed` and rolls back the attempted insertion, preserving
existing orders, FIFO order, and aggregate quantities. Pool capacity is a byte
budget, so the number of orders depends on the implementation's list-node size.
Book construction can still throw if backing-storage allocation fails.

Tests and full-book Add/Cancel benchmarks compare the pooled book against the
implementation before pool integration. Concurrency remains future work.

## Layout

```text
include/order_book/
  core/                  # types, events, book, registry
  memory/                # typed pool, byte pool, fixed-slot pool, adaptor
  io/                    # CSV parsing and writing
src/
  core/                  # order-book implementation
  io/                    # CSV implementation
tests/
  core/                  # order-book contract tests
  memory/                # pool and allocator contract tests
  io/                    # CSV tests
benchmarks/
  benchmark_order_book.cpp
  benchmark_book_allocators.cpp
  support/               # frozen heap baseline, inspection and allocation probe
  support/slot_allocator.hpp  # reference allocator experiment
tools/                  # build, test, replay, generation, profiling scripts
cmake/                  # compiler warnings and sanitizer helpers
data/                   # sample CSV and generated workloads
```

Public headers use the `order_book/` prefix:

```cpp
#include "order_book/core/order_book.hpp"
#include "order_book/memory/memory_pool.hpp"
#include "order_book/io/event_io.hpp"
```

Pool templates live in headers under `order_book::memory`; they do not need
`.cpp` files. The reference slot arena/allocator stays in `benchmarks/support/`
rather than the public library headers. Its allocation algorithms and original
performance claims are preserved; those claims are not measured project results.

## Requirements

- Linux
- CMake 3.25+ for the supplied version-6 presets (plain CMake configuration needs 3.20+)
- Ninja
- GCC or Clang with C++20 support
- Git when `ORDER_BOOK_FETCH_DEPS=ON`
- `perf` for profiling

GoogleTest and Google Benchmark are fetched automatically when not installed.
Set `ORDER_BOOK_FETCH_DEPS=OFF` to use installed dependencies without fetching.

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

## Tests

```bash
./tools/run_tests.sh
```

The default runs both `debug` and `asan` (AddressSanitizer +
UndefinedBehaviorSanitizer). Pass preset names to limit the run:

```bash
./tools/run_tests.sh debug
./tools/run_tests.sh asan
```

Tests cover core behavior and invariants, CSV validation/round trips, and memory
contracts: exhaustion, alignment, constructor failure, object destruction,
stale/invalid handles, repeated slot reuse, byte-pool coalescing, fixed-slot
configuration, allocator rebinding, and `std::list` integration. Reference-arena
tests cover growth and reuse after its final live allocation is returned. They
also build when `ORDER_BOOK_BUILD_BENCHMARKS=OFF`.

`order_book_allocation_tests` checks FIFO, slot return after cancel/full execution,
reuse after level removal, and rollback on pool, map, index, and rehash allocation
failures. It compares full state against the heap baseline after every event.
Its allocation-failure probe is isolated from the production library.

Run only memory tests after building:

```bash
./build/debug/order_book_tests \
  --gtest_filter='MemoryPool.*:BytePool.*:FixedSlotPool.*:AllocatorAdaptor.*:*Reference.*'
```

## Memory contracts

- `MemoryPool<T, N>` constructs/destructs typed objects and returns a handle with
  a pointer, slot index, and generation. Exhaustion returns a null handle;
  constructor exceptions propagate without consuming a slot.
- `Pool<Bytes>` in `memory/byte_pool.hpp` allocates variable-sized byte ranges and
  coalesces freed ranges. `count()` reports free bytes, including padding once
  the corresponding allocation is returned. Per-byte metadata makes this an
  experiment rather than a compact general allocator.
- `FixedSlotPool<Bytes>` fixes its size/alignment on the first successful
  nonzero allocation. Slot sizes must be multiples of their alignment, as with
  `sizeof(T)`/`alignof(T)`. Different configurations are rejected. It allocates
  backing storage at construction and slot metadata on first use.
- `AllocatorAdaptor<T, Bytes>` supports single-element allocations, suited to
  node-based containers such as `std::list`; arrays and hash bucket allocations
  are not supported.
- Raw-pointer pools require pointers originating from that pool and returned
  with the matching byte count. They do not provide generation-based stale
  pointer protection. All pools are single-threaded and must outlive their users.
- The reference `SlotArena` reuses storage only after all live allocations have
  been returned. A continuously nonempty container with insert/erase churn can
  grow its storage; it is not a free-list replacement for that workload.

## Generate and replay workloads

```bash
./build/release/generate_events data/events.csv 1000000 42
./tools/run.sh release data/events.csv
```

Generator arguments are `<output.csv> <event_count> <seed>`. It produces valid
add/cancel/execute sequences with a deterministic seed.

`./tools/run.sh` defaults to the release preset and `data/sample_events.csv`.
Replay reports event count, elapsed time, ns/event, million events/second, and
remaining orders/price levels, then checks book invariants. CSV parsing is
included in replay timing.

Replay stops on rejected updates, including `allocation_failed` when the active
order set exceeds the pool's fixed capacity; large generated workloads may need
a larger pool budget before they can be replayed successfully.
```text
generate_events <output.csv> <event_count> <seed> <instrument_id>
```

The generator emits valid add/cancel/execute sequences for one instrument and keeps
active-order state internally. The instrument ID defaults to `1` when omitted.

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
- instrument count
- remaining orders and level counts

The CSV parser is intentionally included in replay timing. Later, create a second benchmark that preloads decoded events to separate parsing cost from book-update cost.
>>>>>>> main

## Benchmarks

```bash
./tools/run_benchmarks.sh release results
```

Results are written to `results/benchmark.csv` by `order_book_allocator_benchmarks`.
It compares the full pooled book with the frozen heap implementation from commit
`7fbcf06`, using identical predecoded events at 128 and 256 active orders:

- First-use fill/clear, including the pool's lazy metadata allocation.
- Repeated fill/clear after a warm-up cycle.
- Add/Cancel churn while the book stays nonempty.

Before timing, every event's resulting state and FIFO order are compared and
invariants checked. Book construction, backing-storage allocation, warm-up, seed
orders, and state checks are excluded from timing. Heap allocation calls and
requested bytes per event are collected in a separate untimed replay; these
counters do not measure retained memory or RSS. The inactive allocation probe
still adds overhead to allocation calls during timing.

Reported time is per event batch; divide CPU time by the batch's event count
(2N for fill/clear, 8N for churn) to obtain ns/event. The comparison includes the
new Add rollback logic as well as allocator changes. Measure on a stable machine
before drawing performance conclusions.

The older `order_book_benchmarks` target retains its add-only workloads at 1,000
and 10,000 orders. It reports an error when an Add fails, including pool exhaustion,
instead of timing rejected updates as successful operations. The reference arena
remains available in `support/slot_allocator.hpp`; this full-book comparison uses
the heap and fixed-slot pool implementations.

## Profiling

```bash
./build/release/generate_events data/events.csv 5000000 42
./tools/perf_stat.sh
./tools/perf_record.sh
perf report -i results/perf.data
```

For a flame graph:

```bash
export FLAMEGRAPH_DIR=/path/to/FlameGraph
./tools/flamegraph.sh
```

Track ns/event, cycles/event, instructions/event, branch misses/event, and cache
misses/event. Compare the same workload and build settings across changes.

## Next steps

1. Choose a pool capacity from realistic peak active-order counts and measure
   retained memory, including list nodes and pool metadata.
2. Extend benchmarks with mixed add/cancel/execute updates using fixed seeds;
   collect latency distributions and compare reference-arena behavior.
3. Compare one storage change at a time: pool vs heap, linked FIFO vs intrusive
   storage, ordered maps vs indexed price ladders, and hash-index alternatives.
4. Add a reader/parser -> SPSC queue -> single-writer pipeline only after the
   core's correctness and performance baseline are stable. TSan becomes useful
   at that stage; `./tools/build.sh tsan` prepares the instrumented build.
