# Data

Native historical exchange archives can be replayed directly with
`replay_market_data`; see [the native feed guide](../docs/market_data.md).
Downloaded `.gz`, `.bin`, `.pitch` and partial browser downloads in this directory
are ignored by Git. Start Nasdaq ITCH replay at the beginning of the archive;
use `--symbol AAPL` to select a book without losing its earlier state.

## Committed native sample for another machine

[nasdaq-20190130-AAPL.itch.gz](samples/nasdaq-20190130-AAPL.itch.gz) is **16,661,232 bytes**
(16.7 MB / 15.9 MiB), containing **1,668,782 native messages** and **1,656,597 AAPL book updates**.
It retains the entire AAPL session from the 2019-01-30 Nasdaq archive, including
its Stock Directory, all AAPL message types and global system events. Original
length framing, payloads, timestamps, IDs and relative order are preserved.
Other symbols are removed. No CSV conversion or synthetic end-of-session is used.

Run everything on Linux with C++20, CMake/Ninja, Python 3, zlib development headers
and GNU `time` installed:

```sh
./benchmarks/run_sample.sh
```

Use `./benchmarks/run_sample.sh -h` (or `--help`) for the guide; help does not build
or start a benchmark.

The script builds Release in `build/sample-release`, verifies the sample and FIFO
checkpoints, warms up and runs ten alternating observer-off/on pairs. It pins to
the first CPU available to the process and prints the standalone HTML report path.
Results and metadata go into a new ignored `results/` directory on each invocation.
The script works from any current directory and needs no source/configuration edits.
On Ubuntu/Debian the prerequisite packages are `build-essential cmake ninja-build
python3 zlib1g-dev time`. No test/benchmark-library download is needed for this run.

Optional settings: `LOB_BENCH_CPU=2`, `LOB_BENCH_REPETITIONS=20` and `LOB_BUILD_JOBS=4`.
Additional arguments go to the feed runner, for example
`./benchmarks/run_sample.sh --output-dir /tmp/my-aapl-run` (the output must not exist).

The script rebuilds the current C++ sources before every run. To evaluate an
optimization, first run `./benchmarks/run_sample.sh --output-dir results/aapl-before`,
edit the C++ implementation, then run
`./benchmarks/run_sample.sh --output-dir results/aapl-after`. Compare the two HTML
reports with identical dataset/build/CPU settings and check variation across runs.
Observer off/on pairs measure instrumentation cost; they are separate from the
before/after implementation comparison. This script covers feed decode/routing/LOB;
use the [pipeline benchmark](../docs/pipeline_benchmarks.md) for risk/TCP/journal changes.

To build and verify separately:

```sh
cmake --preset release -DORDER_BOOK_BUILD_TESTS=OFF -DORDER_BOOK_BUILD_BENCHMARKS=OFF
cmake --build --preset release --parallel 4 --target replay_market_data
python3 tools/verify_itch_sample.py --bin-dir build/release
```

The check verifies SHA-256, full-session counts/invariants and FIFO snapshot hashes
at 100,000, 500,000 and 1,000,000 records, plus the final empty book. Independent
Python reconstruction checked every price level, quantity, order ID and FIFO
position at those checkpoints and session end. Peak AAPL orders were 42,774;
the verifier uses an 8 MiB node pool. Expected values and source archive identity
are retained in the [manifest](samples/nasdaq-20190130-AAPL.manifest.json).

For a longer performance comparison, run ten alternating observer-off/on pairs
with a prefix warmup. The command prints the output directory containing the
standalone HTML report and raw JSON:

```sh
python3 tools/run_feed_benchmarks.py \
  --input data/samples/nasdaq-20190130-AAPL.itch.gz --symbol AAPL \
  --compare-instrumentation --repetitions 10 --warmup-records 100000
```

Add `--cpu N` to pin to an available CPU. Compare machines using the same sample
checksum, compiler/build settings and measurement boundaries, and inspect variation
between repetitions. This sample exercises AAPL history; its throughput and memory
usage differ from the full-market archive because routing state for other symbols
is absent. It does not measure network or order-entry matching latency.

To regenerate a sample from a complete local source archive:

```sh
cmake --build --preset release --parallel 4 --target extract_itch_sample
./build/release/extract_itch_sample --input data/01302019.NASDAQ_ITCH50.gz \
  --symbol AAPL --output /tmp/AAPL.itch.gz
```

Extraction refuses to overwrite an existing output and removes a partial file on
read/CRC/session failure. The large original archive remains ignored by Git.

Generated CSV format (older replay/generator tools):

```text
sequence,instrument_id,event_type,order_id,side,price,quantity
1,1001,ADD,1001,BID,10000,25
2,1001,EXECUTE,1001,,,5
3,1001,CANCEL,1001,,,
```

Columns:

- `sequence`: global, strictly contiguous event sequence starting at `1`
- `instrument_id`: numeric identifier used to route the event to an order book
- `event_type`: `ADD`, `CANCEL`, or `EXECUTE`
- `order_id`: identifier of the affected order
- `side`: `BID` or `ASK`; present only for `ADD`
- `price`: integer price in the instrument's smallest price unit; present only for `ADD`
- `quantity`: order quantity for `ADD`, executed quantity for `EXECUTE`, empty for `CANCEL`

Event types:

- `ADD`: add order; `side`, `price`, and `quantity` are required
- `CANCEL`: cancel the remaining order quantity; non-applicable fields are empty
- `EXECUTE`: execute a quantity against an existing order; `quantity` is required

Generate a deterministic workload:

```bash
./build/release/generate_events data/events.csv 1000000 42 1001
```

The final argument is the instrument ID and defaults to `1` when omitted.
