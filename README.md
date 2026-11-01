# C++20 Order Book and Trading System

A single-writer order book with native Nasdaq ITCH/Cboe PITCH replay, a local
price-time matching exchange, TCP order entry, UDP market data and journal recovery.
The main workflow is to verify correctness, measure a baseline, change one C++
bottleneck and repeat the same benchmark.

## Run the native-data benchmark

On Linux, install a C++20 compiler, CMake, Ninja, Python 3, zlib development headers
and GNU `time`. Ubuntu/Debian packages:

```sh
sudo apt install build-essential cmake ninja-build python3 zlib1g-dev time
```

Then run:

```sh
./benchmarks/run_sample.sh
./benchmarks/run_sample.sh -h
```

The script builds current sources in Release, verifies the committed **16.7 MB
AAPL ITCH sample**, warms up and runs ten alternating observer-off/on pairs.
Each replay processes 1,668,782 real messages and 1,656,597 AAPL book updates.
It prints the standalone HTML report path and retains raw JSON, histograms,
compiler settings, source/binary hashes and CPU metadata under ignored `results/`.
No server or Python packages are needed for the HTML report.

To evaluate a C++ optimization:

```sh
./benchmarks/run_sample.sh --output-dir results/aapl-before
# Edit the C++ implementation.
./benchmarks/run_sample.sh --output-dir results/aapl-after
```

Compare the two reports using the same dataset, compiler/build settings and CPU.
The script rebuilds before measuring. `--cpu N` selects an available logical CPU;
`--repetitions N` changes the number of pairs. Reports retain per-run variation.
Off/on measures observer overhead; before/after compares implementations.

This workload covers **feed decode → routing → order book**. It omits other
symbols and does not measure network, risk checks, journal or matching latency.
See the [sample and verification guide](data/README.md),
[native feed contracts](docs/market_data.md) and [HTML report convention](docs/reporting.md).

## Run correctness tests

```sh
./tools/run_tests.sh
./tools/run_tests.sh -h
```

The default builds and tests Debug and ASan/UBSan, then runs the Python reporting
tests. `./tools/run_tests.sh debug` selects one configuration. CMake presets require
CMake 3.25 or newer; the standalone sample script uses plain CMake 3.20 or newer.
GoogleTest is fetched if needed; the native sample benchmark has no test-library
dependency. `LOB_BUILD_JOBS` controls build parallelism (default: 4).

Tests cover order/FIFO invariants, allocation-failure rollback, native feed framing
and semantics, sample extraction/checksums, socket processes, matching, multicast,
risk, shutdown and journal recovery. A faster result is useful only if these
contracts still pass.

## Run the local trading system

```sh
./tools/build.sh release
python3 tools/run_trading_demo.py --bin-dir build/release
python3 tools/run_pipeline_benchmarks.py --bin-dir build/release
```

The finite demo verifies maker/taker clients, private reports, public book updates,
cash/position conservation and durable replay. The pipeline benchmark records
p50/p99/p99.9 and achieved closed-loop throughput with journal off/on. Use it when
changing client risk, queues, TCP, matching or persistence.

See the [trading system guide](docs/trading_system.md) and
[pipeline measurement guide](docs/pipeline_benchmarks.md). These are local simulators
and do not connect to an exchange.

## Further measurements and project layout

| Task | Entry point |
|---|---|
| Native sample, build + verify + HTML benchmark | `./benchmarks/run_sample.sh` |
| Correctness and sanitizers | `./tools/run_tests.sh` |
| Replay another native capture | `build/release/replay_market_data --help` |
| Extract a complete symbol session | `build/release/extract_itch_sample --help` |
| Heap/pool core microbenchmarks | `./tools/run_benchmarks.sh release results` |
| Recorded baselines | [benchmarks/results](benchmarks/results/README.md) |

The optional heap/pool comparison requires matplotlib and Google Benchmark;
its runner enables the benchmark CMake option automatically. It measures batch
averages for first-use fill/clear, warmed reuse and add/cancel churn. Native replay
and pipeline reports measure different paths and should be compared separately.

`include/order_book/` contains core, feed, memory, networking and trading interfaces;
`src/` implements them. `tests/` holds contract/process tests, `tools/` holds the
runners and `benchmarks/` holds the benchmark entry point and core workloads.
The production allocator consists of `FixedSlotPool` and `AllocatorAdaptor`.
The heap book remains as a reference for correctness and allocation comparisons.

The LOB uses ordered price levels, a hash order index and FIFO lists. A book owns
its pool; price maps, indexes and routing metadata also allocate on the heap.
Allocation failure returns an error and rolls back an attempted order update.
The book remains single-writer. Native adapters reconstruct displayed liquidity;
they do not infer hidden orders or prove an exchange's matching decisions.

CSV codecs and the older event-ingress demos remain for existing contract tests:
[CSV format](data/README.md), [TCP event ingress](docs/tcp_first_step.md) and
[UDP multicast ingress](docs/udp_multicast.md). Nasdaq has a complete historical
archive measurement; Cboe is validated with specification fixtures so far. HOSE/HNX
need their own feed specifications.
