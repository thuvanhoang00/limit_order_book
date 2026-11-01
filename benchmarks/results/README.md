# Recorded benchmark baselines

Selected benchmark runs are committed here with charts, raw repetitions and machine/build metadata. Ordinary runs and smoke checks remain local under the ignored `results/` directory.

| Date | Measured source | Report |
|---|---|---|
| 2026-10-27 | `162f9cd` | [Baseline 01: heap vs pooled Add/Cancel](20261027-201459-baseline-01-162f9cd/README.md) |
| 2026-11-01 | `b58a194` + measurement harness and recorded client patch | [Client queue wakeup: TCP/UDP pipeline, journal on/off](20261101-pipeline-wakeup/README.md) |
| 2026-11-01 | `c2c4825` + exact latency instrumentation | [Complete Nasdaq ITCH archive: latency and throughput](20261101-full-itch-latency/README.md) |

Open a baseline README to view the PNG charts directly on GitHub. Each snapshot also includes SVG charts, HTML, original Google Benchmark JSON, normalized CSVs, exact invocation, build configuration and validation logs. Compare results only with matching hardware, affinity, compiler, workload and timing settings.
