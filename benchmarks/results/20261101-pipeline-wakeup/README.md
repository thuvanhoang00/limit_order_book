# Client queue wakeup: local pipeline measurements

Measured 2026-11-01 on AMD Ryzen 3 5300U, GCC 11.4.0, Release `-O3 -DNDEBUG`,
CPU affinity 0–7, ext4 (`/dev/sda2`), loopback TCP/multicast, diagnostic logging enabled.
Three repetitions per version/journal mode; each has 8,000 measured commands and
800 warmup commands. Values below are medians of the three per-run statistics,
not percentiles of pooled samples. All 96,000 measured samples were independently
recomputed from CSV; matching/feed/replay checks passed in all twelve runs.

The baseline is `b58a194faabb67013e0458872c7d8ac17295bc23` plus the new measurement
harness. The after build changes only client queue notifications in production
code; [optimization.patch.gz](optimization.patch.gz) records the exact change. Metadata
identifies the frozen binaries and source files with SHA-256. The benchmark source
is `tools/trading_pipeline_benchmark.cpp` in this change.

## Submit to private Complete

| Client | Journal | p50 (µs) | p99 (µs) | p99.9 (µs) | Commands/s |
|---|---|---:|---:|---:|---:|
| before | off | 5,299.56 | 7,938.49 | 9,932.82 | 192.0 |
| after | off | 93.98 | 179.53 | 955.04 | 7,776.9 |
| before | on | 14,860.95 | 28,606.11 | 88,723.04 | 66.1 |
| after | on | 9,955.84 | 17,921.40 | 52,774.50 | 95.8 |

## Submit to Complete and local market application

| Client | Journal | p50 (µs) | p99 (µs) | p99.9 (µs) | Commands/s |
|---|---|---:|---:|---:|---:|
| before | off | 5,301.71 | 8,077.25 | 10,772.03 | 192.0 |
| after | off | 112.58 | 230.79 | 1,322.82 | 7,776.9 |
| before | on | 14,864.24 | 29,287.47 | 88,732.41 | 66.1 |
| after | on | 9,964.69 | 17,951.60 | 52,778.92 | 95.8 |

Removing the client's 5 ms gateway polling delay and 1 ms report polling sleeps
raised achieved journal-off closed-loop throughput by 40.5x. Journal-on
p50 drops by about 5 ms while the per-command durable sync contract stays intact.
This is a single globally outstanding command; it does not establish saturation
capacity or latency under independent offered load.

This was a desktop run with normal CPU scaling/background applications. Early
baseline journal-on repetitions overlapped local builds/tests; later baseline and
all after repetitions did not. In particular, journal tail statistics describe
these runs and are not controlled causal estimates of the optimization. Each
p99.9 has only eight samples above its rank. The strongest queue-wakeup evidence
is the consistent removal of the millisecond-scale journal-off latency floor.

Selected evidence: [before summaries](before/summary.json),
[after summaries](after/summary.json), [comparison](comparison.json),
[verification](verification.json), metadata and compressed CMake caches in each directory,
and compressed per-command CSVs named `*-journal-*.samples.csv.gz`.
Full logs, journals, replay JSON, executable/source snapshots and exact commands
remain locally under `results/pipeline-before-wakeup` and
`results/pipeline-after-wakeup`.

The full Debug and ASan/UBSan CTest suites passed 141/141 each, including seventeen
trading process scenarios and the new benchmark/raw-statistics/replay scenario.
Existing Python reporting tests passed 6/6. No timing threshold was added to CI.

Reproduce the measurement protocol against each version:

```sh
python3 tools/run_pipeline_benchmarks.py --bin-dir build/release \
  --iterations 1000 --warmup 100 --repetitions 3
```

See [the measurement guide](../../../docs/pipeline_benchmarks.md) for boundaries
and the default longer workload.
