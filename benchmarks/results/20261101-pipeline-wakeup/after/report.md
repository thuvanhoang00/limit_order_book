# Local pipeline benchmark

Closed loop: one command outstanding globally across two real TradingClients. Each command waits for Complete and its UDP watermark to be applied locally. Throughput includes reference-report checks between commands; it is achieved closed-loop throughput, not saturation capacity.

| Journal | Run | Complete p50 / p99 / p99.9 (µs) | Market applied p50 / p99 / p99.9 (µs) | Commands/s |
|---|---|---|---|---|
| off | 01-journal-off | 93.98 / 177.66 / 238.08 | 112.84 / 228.24 / 271.75 | 7830.3 |
| on | 01-journal-on | 9955.84 / 17921.40 / 52774.50 | 9964.69 / 17951.60 / 52778.92 | 95.8 |
| on | 02-journal-on | 9966.30 / 16607.89 / 43422.82 | 9973.87 / 16646.82 / 43423.98 | 97.1 |
| off | 02-journal-off | 91.62 / 179.53 / 955.04 | 112.58 / 230.89 / 1322.82 | 7776.9 |
| off | 03-journal-off | 94.86 / 183.56 / 1135.56 | 112.53 / 230.79 / 1323.48 | 7623.0 |
| on | 03-journal-on | 9909.84 / 28633.99 / 99380.48 | 9916.82 / 28637.69 / 99383.22 | 93.7 |

Medians across repetitions:

- Journal off: 7776.9 commands/s; Complete p99 179.53 µs.
- Journal on: 95.8 commands/s; Complete p99 17921.40 µs.

Percentiles use nearest rank on individual command samples. Startup, reference generation, warmup, shutdown and CSV output are excluded. Complete includes client risk checks, client queues, TCP, server queues, matching, private reports, and fdatasync when journaling is enabled. Market applied ends after Complete AND local application of the corresponding UDP deltas; it is not a one-way multicast latency. Both clients must stay synchronized without snapshot recovery during the run. Per-command samples, metrics and durable replay checks are retained.
