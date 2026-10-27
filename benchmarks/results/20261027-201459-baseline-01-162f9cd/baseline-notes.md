# Baseline 01 — 2026-10-27

Measured commit: `162f9cd0936bfd0f1e3d9f52ba499852bbb4630e` on branch `main`; clean source tree. Run ID: `20261027-201459-baseline-01-162f9cd`.

CPU: AMD Ryzen 3 5300U; GCC 11.4.0; Release `-O3 -DNDEBUG`; sanitizers off. Google Benchmark 1.9.4.78. Pinned to logical CPU 2 (SMT sibling 3); governor remained powersave. Each of 12 cases has 10 repetitions, minimum 2 seconds of timed CPU work per repetition, with random interleaving. No system power/boost/governor settings were changed.

Release validation: 89/89 tests passed before timing. Each benchmark also checks pooled/heap state equivalence and invariants outside timing. All 120 recorded repetitions completed without errors.

## Results

Median CPU ns/event, calculated from the ten repetition-level batch averages. CV is sample standard deviation divided by mean across repetitions. A positive reduction means the pooled implementation used less CPU time.

| Workload | Active orders | Heap ns/event | Pool ns/event | Pool reduction | Heap CV | Pool CV |
|---|---:|---:|---:|---:|---:|---:|
| Fill/clear, first use | 128 | 46.92 | 55.16 | -17.6% | 1.8% | 2.2% |
| Fill/clear, first use | 256 | 51.85 | 47.51 | +8.4% | 1.2% | 0.9% |
| Fill/clear, reused pool | 128 | 42.27 | 39.60 | +6.3% | 0.8% | 0.9% |
| Fill/clear, reused pool | 256 | 39.27 | 34.92 | +11.1% | 1.2% | 0.8% |
| Continuous Add/Cancel | 128 | 35.54 | 32.72 | +8.0% | 1.0% | 0.8% |
| Continuous Add/Cancel | 256 | 34.14 | 32.41 | +5.1% | 0.9% | 0.8% |

## Heap allocation traffic

These counters come from separate untimed allocation-count passes. Requested bytes/event do not represent retained memory or RSS and exclude book construction/backing-storage allocation.

| Workload | Active orders | Heap allocations/event | Pool allocations/event | Heap requested B/event | Pool requested B/event |
|---|---:|---:|---:|---:|---:|
| Fill/clear, first use | 128 | 1.144531 | 0.652344 | 56.156 | 57.406 |
| Fill/clear, first use | 256 | 1.072266 | 0.576172 | 44.078 | 36.703 |
| Fill/clear, reused pool | 128 | 1.125000 | 0.625000 | 41.000 | 26.000 |
| Fill/clear, reused pool | 256 | 1.062500 | 0.562500 | 36.500 | 21.000 |
| Continuous Add/Cancel | 128 | 1.000000 | 0.500000 | 32.000 | 16.000 |
| Continuous Add/Cancel | 256 | 1.000000 | 0.500000 | 32.000 | 16.000 |

## Scope and comparison rules

- This is the existing core Add/Cancel workload suite. Execute, CSV parsing, registry dispatch, logger enqueue/write overhead, and queue throughput are not measured.
- Fill/clear batches contain 2N events; churn batches contain 8N events. Dividing batch times by those event counts yields the reported ns/event.
- Construction/destruction outside the timed region, seeding, warm-up, correctness checks, and allocation counting are excluded. Inactive allocation-probe overhead remains included.
- The heap version is the frozen pre-pool implementation. Differences include the pooled implementation's allocation-failure rollback logic, not just allocator selection.
- CPU scaling was enabled and the machine was not isolated from other applications. This is a local baseline, not a universal hardware claim. Consult CV and raw repetitions when assessing small changes.
- These figures are averages per event within batches, then medians across repetitions; they are not per-order latency percentiles.
- For the next measurement, keep hardware, logical CPU, compiler/build flags, benchmark-library version, workloads, capacity, logging settings, repetition count and minimum timing identical. Use a new output directory and record the new source commit. Compare each implementation against its own baseline as well as the heap/pool pair.

## Files

- `benchmark.json`: original Google Benchmark output with all repetitions and aggregates.
- `repetitions.csv`: all 120 repetitions normalized to CPU/wall ns/event.
- `summary.csv`: normalized medians, means, dispersion, and allocation counters.
- `metadata.json`: source, command, machine, protocol, binary hash, start/end snapshots.
- `command.txt`: exact invocation.
- `console.txt`, `build.log`, `tests.log`, `dry-run.log`: verification and runtime output.
- `CMakeCache.txt`, `compile_commands.json`: actual build configuration.
- Run and rendering procedures are now provided by `tools/run_benchmarks.sh` and `tools/benchmark_report.py` in the repository.

The measured source tree was clean. This selected baseline snapshot is now committed under `benchmarks/results/`; ad hoc runner/analysis scripts remain in the original local run directory.
