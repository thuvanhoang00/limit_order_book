# C++20 Order Book and Local Trading System

A single-writer order-book baseline with implemented add, cancel, partial/full
execution, best bid/ask queries, reset, and invariant validation. The project also
includes CSV generation/replay, TCP and UDP multicast event ingress, contract tests, profiling tools, and standalone
memory-pool experiments.

## Run a complete local trading scenario

The exchange simulator accepts orders over TCP, matches by price-time priority,
returns ACKs/private fills, and publishes sequenced market data over UDP multicast.
Trading apps maintain local books, orders, risk and positions/PnL. Limit/market,
IOC/FOK, post-only, cancel/replace, self-trade prevention, journal restart recovery,
maker/taker strategies and snapshot recovery are implemented.

```sh
cmake --preset release
cmake --build --preset release --parallel 4
python3 tools/run_trading_demo.py --bin-dir build/release
```

The finite demo verifies two real TCP clients, matching, multicast book updates,
cash/position conservation and durable replay. It retains logs and JSON evidence
under `results/`. See [the trading system guide](docs/trading_system.md) for the
architecture, scripts, configuration, wire format, benchmarks and remaining scope.
This runs locally and does not connect to a real exchange.

## Current LOB scope

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
implementation before pool integration. The book remains single-writer; an optional
SPSC logger writes records asynchronously on a dedicated worker.

## Layout

```text
include/order_book/
  core/                  # types, events, book, registry, logger, SPSC queue
  memory/                # typed pool, byte pool, fixed-slot pool, adaptor
  io/                    # CSV parsing and writing
  net/                   # TCP/UDP multicast transports, shared fd ownership, wire codec
src/
  core/                  # order-book implementation
  io/                    # CSV implementation
  net/                   # Linux blocking sockets and wire codec
tests/
  core/                  # order-book contract tests
  memory/                # pool and allocator contract tests
  io/                    # CSV tests
  net/                   # transport, protocol and TCP/multicast process tests
benchmarks/
  benchmark_order_book.cpp
  benchmark_book_allocators.cpp
  support/               # frozen heap baseline, inspection and allocation probe
  support/slot_allocator.hpp  # reference allocator experiment
tools/                  # build, test, replay, generation, profiling scripts
cmake/                  # compiler warnings and sanitizer helpers
data/                   # sample CSV and generated workloads
```

Public C++ headers use `.hpp` and the `order_book/` prefix:

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
- Python 3 and matplotlib for benchmark reports (`python3 -m pip install matplotlib`)
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

## TCP event ingress

The TCP server applies `MarketEvent` frames to the same multi-instrument registry
used by CSV replay. It binds to loopback, accepts one client, and exits after that
session. Build using the existing presets, then run in two terminals:

```bash
./build/debug/tcp_event_server 9000 results/tcp_server.log
./build/debug/tcp_event_client data/sample_events.csv 9000 results/tcp_client.log
```

The client sends each decoded CSV event as a 40-byte binary frame and waits for
its 16-byte apply-result ACK before sending the next. Sequence starts at 1 and
is consecutive globally across instruments. Validation does not consume the
sequence: only a successful apply advances it. Rejected updates return their
`ApplyResult`; the sample client exits, while a custom client can correct and
retry that sequence on the same connection. Invalid sequences or malformed frames
close the connection before applying the event. ACKs confirm in-memory updates.

Server/client logging defaults to `tcp_server.log` / `tcp_client.log` when the
last argument is omitted. Both record connection lifecycle, ACKs and failures;
the server also logs every book operation through the registry. Both drain their
logs and report dropped records, formatting failures and write failures on stderr.
They return failure on formatting/write errors.

Networking builds by default on Linux. Set `ORDER_BOOK_BUILD_NETWORKING=OFF` to
exclude it. Python 3 enables the two-process tests in the regular `check` target;
protocol/transport tests use GoogleTest. This first step uses blocking sockets,
with one book writer, no socket deadlines, and no state recovery across sessions.
See [the TCP protocol and session guide](docs/tcp_first_step.md) for wire format,
error behavior, and implementation details.

## UDP multicast event ingress

The multicast subscriber applies the same 40-byte event format directly to its
single-writer registry. Start subscribers before publishing; one publisher can
update multiple independent books. There are no ACKs. Stale packets are dropped;
gaps, malformed packets and rejected updates stop the feed before further apply.
The demo uses a known event count and receive timeout; recovery is a future step.

See [UDP_QUICKSTART.md](UDP_QUICKSTART.md) for local build/run commands and
[the UDP guide](docs/udp_multicast.md) for socket setup and sequencing policy.

## Asynchronous logging

Replay appends to `order_book.log` by default. Supply a separate path per process
or run when needed (parent directories must already exist):

```bash
./build/debug/replay_order_book data/sample_events.csv results/replay.log
```

`Logger` uses `LFQueue<LogRecord, 4096>` as a bounded single-producer,
single-consumer queue. Only the producer captures time/source and copies a fixed
record; the worker formats and writes it. Queue-full attempts are dropped without
waiting and counted by `dropped_count()`. This is best-effort diagnostic logging,
so even error records can be dropped under saturation.

The registry records every attempted add/cancel/execute with sequence, instrument,
order, side, price/quantity, and `ApplyResult`. Successful updates use DEBUG, rejected
updates WARN, and allocation failures ERROR. Replay logs start/finish at INFO and
input, parsing, sequence, and invariant errors at ERROR/FATAL. Detailed parser and
invariant reasons remain on stderr. The direct `OrderBook` API stays independent
of logging; use the registry when operation logs are needed.

```cpp
#include "order_book/core/logger.hpp"
#include "order_book/core/order_registry.hpp"

order_book::Logger logger("orders.log", order_book::LogLevel::Warn);
order_book::OrderBookRegistry registry(&logger);
// Apply events from one producer thread. Declare logger before registry so it
// outlives it. Omitting the logger keeps logging disabled for library callers.
```

Logger level defaults to DEBUG; `LogLevel::Off` disables records. A logger shared
by registries still requires exactly one producer thread. `close()` is idempotent:
stop producing first, then close to join the worker and drain accepted records.
The destructor does the same. Replay reports `log_dropped`, `log_format_failures`,
and `log_write_failed` after closing, and returns failure on formatting/write
errors. An invalid log path also fails replay. Dropped records do not fail replay.
Replay timing includes enqueue overhead but excludes the final drain; compare
benchmarks with the same logging settings. Book benchmarks do not attach a logger.

Tests cover ring reuse/full/empty handling, concurrent SPSC record integrity,
filtering, draining on shutdown, append behavior, saturation accounting, write
failure, and registry result/context logging, including pool exhaustion.

## Benchmarks

```bash
./tools/run_benchmarks.sh release results
```

Each run creates a new `results/<timestamp>-<commit>/` directory instead of
replacing previous results. Open its `report.html` for the comparison table and
charts, or view `latency.png` and `allocations.png` directly. SVG versions are
also saved. Bars show repetition medians, error bars show sample standard
deviation, and dots show individual repetition means. These are batch averages
per event, not per-order latency percentiles.

The directory also retains all repetitions in `benchmark.json` and
`repetitions.csv`, normalized statistics in `summary.csv`, a `summary.md` report,
`metadata.json`, exact invocation, console/build logs, and the actual CMake cache
and compiler commands. Dirty worktrees are labeled in the report and their
tracked patch and untracked project sources are saved for traceability.
`results/` remains gitignored. Selected historical runs, including their PNG/SVG
charts, are committed under [benchmarks/results](benchmarks/results/README.md).

Defaults are 2 seconds of timed CPU work per repetition, 10 repetitions per case,
and random interleaving. Pin to the same logical CPU across comparison runs:

```bash
./tools/run_benchmarks.sh release results --cpu 2
# Short smoke check of the reporting pipeline; do not use as a baseline:
./tools/run_benchmarks.sh release results/smoke --min-time 0.01s --repetitions 2
# Re-render existing raw JSON without running benchmarks again:
python3 tools/benchmark_report.py render results/<run-directory>
# Verify normalization and input validation:
python3 -m unittest discover -s tests/tools -v
```

Keep hardware, CPU affinity, governor/boost settings, compiler/build flags,
benchmark-library version, workloads and timing protocol consistent when
comparing runs. Metadata records the environment without changing system power
settings. Reports measure the core with logging disabled; they do not measure
Execute events, CSV parsing, registry dispatch or logger overhead.

`order_book_allocator_benchmarks` compares the full pooled book with the frozen heap implementation from commit
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
4. Profile the implemented TCP -> SPSC -> matching -> UDP pipeline, including
   queueing and journal latency; `./tools/build.sh tsan` prepares a race-instrumented
   build. Add private gateway reconciliation and journal checkpoints next.
