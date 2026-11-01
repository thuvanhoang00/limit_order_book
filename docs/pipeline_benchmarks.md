# Measuring the local trading pipeline

Build Release and run the journal on/off comparison:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
python3 tools/run_pipeline_benchmarks.py --bin-dir build/release
# Small functional check, not a performance baseline:
python3 tools/run_pipeline_benchmarks.py --iterations 16 --warmup 2 --repetitions 1
# Optional: use a fixed set of available CPUs for the driver and all workers.
python3 tools/run_pipeline_benchmarks.py --cpus 2,3,4,5
```

The default is three repetitions of 40,000 measured commands per journal mode,
with 800 warmup commands on each fresh server. Journal modes alternate order
between repetitions. Every run uses new TCP/UDP ports, a fresh journal and local
multicast loopback. Diagnostic logging remains enabled in both modes. Journaling
enabled means the existing command-by-command `fdatasync` contract, before matching
and private reports; disabled passes `--journal -` to the same server.

`trading_pipeline_benchmark` uses two actual `TradingClient`s, including their
gateway/subscriber threads, bounded queues, local risk checks, order managers,
features and market replicas. Each eight-command cycle adds an ask, reduces it,
matches two lots and cancels the remainder, then repeats on the bid side. Both
accounts finish every cycle flat with zero net cash, and the exchange book is
empty. Reference commands/results are precomputed before warmup or measurement.
Existing independent matching-model and recovery tests remain the correctness
checks for the engine itself; this benchmark checks pipeline consistency.

## What the numbers measure

- **Complete**: time immediately before `TradingClient::submit()` until it returns
  the terminal private `Complete`. This includes local risk and queueing, client
  gateway wakeup, TCP encode/send/receive, server queueing, journal sync when enabled,
  matching and report handling.
- **Market applied**: same start, ending after Complete **and** the submitting
  client's replica has consumed all UDP deltas through the expected watermark.
  This includes publisher/subscriber queues, multicast and local feature/book
  updates. It is not an independent one-way feed timestamp; deltas can arrive
  before Complete.
- **Commands/s**: completed measured commands divided by wall time across the
  measured loop, including the short report-verification work between commands.
  Startup, reference generation, warmup, final cross-client drain, shutdown,
  sample sorting and CSV output are excluded.

There is **one command in flight globally**, and the next command waits for both
Complete and its market watermark. This measures achieved closed-loop throughput
and latency at that offered load. It does not establish saturation capacity or
tail latency under an independent arrival rate; it cannot expose queue buildup
from an open-loop producer. No orders/second claim for production or a physical
network follows from this loopback benchmark.

Percentiles use nearest rank over individual command durations: rank
`ceil(sample_count * percentile)`. p99.9 with 8,000 commands has only eight
observations in its upper 0.1%; use larger workloads and repeated runs for stronger
tail evidence. Compare the same CPU set, compiler/flags, journal filesystem,
workload and logging settings on an otherwise idle host. Journal storage and
scheduler behavior materially affect the result.

## Evidence and failure behavior

Each invocation creates a new `results/pipeline-*` directory; an existing directory
is never overwritten. It contains:

- `report.md`, all repetitions in `summary.json`, and raw per-command `samples.csv`.
- Per-run exchange metrics/logs, exact child-process commands, client output,
  journal and replay JSON when enabled, and individual verification checks.
- Platform, CPU model/affinity, invocation, Git revision/dirty state and SHA-256
  identities in `metadata.json`; CMake cache/compiler commands when available.
- Frozen copies of the three executables and relevant source files plus the
  tracked patch. Rebuilding the checkout while a run is active cannot change
  which executable that run uses.

Private reports must match the untimed reference engine, including fill prices,
quantities, IDs and remaining quantities (session epochs/report sequence numbers
are ignored). Both accounts must end with zero open orders, zero positions/net
cash, expected executed volumes and empty, synchronized replicas. No additional
snapshot recovery may occur during the workload. Server command, trade, volume
and feed counts must match. Durable replay must reproduce the counts, final feed
sequence and empty book. Failed checks stop the invocation; its incomplete
artifacts remain available for diagnosis.

The process test runs both journal modes, recalculates every reported percentile
and throughput from raw samples, checks matching/replay, and verifies that reruns
cannot overwrite evidence. It has no machine-specific latency threshold.

## Queue-wakeup optimization

The original client gateway inspected its outgoing queue before polling the socket
for up to 5 ms; submitting to the queue did not wake that poll. The application
also checked incoming private reports between 1 ms sleeps. An otherwise idle
command could therefore spend milliseconds waiting despite a fast matching engine.

The client now signals a nonblocking Linux `eventfd` after publishing a request
or report. The gateway polls the request signal alongside its socket; report
waits poll the report signal against the existing deadline. Queue reads always
precede waiting, and signals persist/coalesce until drained, so publication during
the transition to sleep does not lose a wakeup. Stop/failure paths also signal
waiters. The single-producer/single-consumer queues and matching/journal semantics
are unchanged.

The [recorded experiment](../benchmarks/results/20261101-pipeline-wakeup/README.md)
retains three repetitions of 8,000 measured commands for each version/journal mode.
Median per-run Complete statistics were:

| Client | Journal | p50 (µs) | p99 (µs) | p99.9 (µs) | Commands/s |
|---|---|---:|---:|---:|---:|
| before | off | 5,299.56 | 7,938.49 | 9,932.82 | 192.0 |
| after | off | 93.98 | 179.53 | 955.04 | 7,776.9 |
| before | on | 14,860.95 | 28,606.11 | 88,723.04 | 66.1 |
| after | on | 9,955.84 | 17,921.40 | 52,774.50 | 95.8 |

The journal-off throughput increase was 40.5x for this closed-loop workload.
All twelve runs passed matching, account, feed and applicable durable replay checks;
all 96,000 samples were checked again against the reported statistics. Debug and
ASan/UBSan suites each passed 141/141 tests, and Python reporting tests passed 6/6.

These measurements used normal desktop CPU scaling/background applications. Early
baseline journal-on runs overlapped builds/tests, so journal tails are descriptive
rather than controlled optimization estimates. Later baseline repetitions and
all after repetitions had no local build/test overlap. See the recorded report
for market-application statistics, raw samples, source identities and limitations.
