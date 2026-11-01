# Local pipeline benchmark

Closed loop: one command outstanding globally across two real TradingClients. Each command waits for Complete and its UDP watermark to be applied locally. Throughput includes reference-report checks between commands; it is achieved closed-loop throughput, not saturation capacity.

| Journal | Run | Complete p50 / p99 / p99.9 (µs) | Market applied p50 / p99 / p99.9 (µs) | Commands/s |
|---|---|---|---|---|
| off | 01-journal-off | 5300.58 / 6350.31 / 7555.67 | 5302.23 / 6353.89 / 7796.36 | 194.6 |
| on | 01-journal-on | 14891.30 / 37952.08 / 88723.04 | 14893.40 / 37957.14 / 88732.41 | 66.1 |
| on | 02-journal-on | 14860.95 / 28606.11 / 158136.42 | 14864.24 / 29287.47 / 158137.29 | 62.6 |
| off | 02-journal-off | 5299.56 / 8007.57 / 9932.82 | 5301.71 / 8293.06 / 10772.03 | 192.0 |
| off | 03-journal-off | 5299.44 / 7938.49 / 10237.76 | 5301.29 / 8077.25 / 10955.09 | 191.9 |
| on | 03-journal-on | 14851.28 / 21509.08 / 25694.25 | 14854.26 / 21545.11 / 25694.85 | 67.9 |

Medians across repetitions:

- Journal off: 192.0 commands/s; Complete p99 7938.49 µs.
- Journal on: 66.1 commands/s; Complete p99 28606.11 µs.

Percentiles use nearest rank on individual command samples. Startup, reference generation, warmup, shutdown and CSV output are excluded. Complete includes client risk checks, client queues, TCP, server queues, matching, private reports, and fdatasync when journaling is enabled. Market applied ends after Complete AND local application of the corresponding UDP deltas; it is not a one-way multicast latency. Both clients must stay synchronized without snapshot recovery during the run. Per-command samples, metrics and durable replay checks are retained.
