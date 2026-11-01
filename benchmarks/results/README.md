# Recorded benchmark baselines

Selected benchmark runs are committed here with raw measurements and machine/build metadata. Ad hoc runs go to the ignored `results/` directory; old duplicates and smoke checks can be removed once useful evidence has been archived.

| Date | Measured source | Report |
|---|---|---|
| 2026-10-27 | `162f9cd` | [Baseline 01: heap vs pooled Add/Cancel](20261027-201459-baseline-01-162f9cd/README.md) |
| 2026-11-01 | `b58a194` + measurement harness and recorded client patch | [Client queue wakeup: TCP/UDP pipeline, journal on/off](20261101-pipeline-wakeup/README.md) |
| 2026-11-01 | `c2c4825` + exact latency instrumentation | [Complete Nasdaq ITCH archive: latency and throughput](20261101-full-itch-latency/README.md) |
| 2026-11-01 | Frozen C++ sources matching `0e478e6` | [ITCH observer overhead, CPU hotspots and slow records (HTML)](20261101-itch-observer-overhead/report.html) |

Open HTML reports locally. Historical baseline READMEs also show exported charts directly on GitHub. Snapshots retain selected raw JSON/CSV or compressed histograms, exact commands, build configuration and verification evidence as appropriate to each workload. Compare results only with matching hardware, affinity, compiler, workload and timing settings.
