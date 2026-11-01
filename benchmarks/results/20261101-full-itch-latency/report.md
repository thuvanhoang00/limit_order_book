# Full-archive ITCH replay latency

Offline service times on each input record; no network or order-entry matching. All records are measured, including the first and final exchange records. Histograms and three clock reads per record add overhead; throughput is for the instrumented replay. Gzip is buffered, so read/decompression cost is concentrated in refill records.

| Run | Path | Count | p50 (ns) | p99 (ns) | p99.9 (ns) | Max (ns) |
|---|---|---:|---:|---:|---:|---:|
| 1 | read_decode_apply | 368,366,634 | 621 | 3,256 | 5,440 | 4,335,104 |
| 1 | decode_apply | 368,366,634 | 561 | 3,176 | 5,300 | 4,335,054 |
| 1 | selected_update_decode_apply | 1,656,597 | 1,333 | 6,703 | 11,301 | 4,335,054 |
| 1 | clock_pair_overhead | 10,000 | 20 | 31 | 31 | 641 |
| 2 | read_decode_apply | 368,366,634 | 641 | 3,156 | 5,320 | 5,195,874 |
| 2 | decode_apply | 368,366,634 | 571 | 3,076 | 5,170 | 5,195,824 |
| 2 | selected_update_decode_apply | 1,656,597 | 1,353 | 6,362 | 10,980 | 5,195,824 |
| 2 | clock_pair_overhead | 10,000 | 20 | 31 | 31 | 581 |
| 3 | read_decode_apply | 368,366,634 | 621 | 3,166 | 5,360 | 3,855,969 |
| 3 | decode_apply | 368,366,634 | 561 | 3,086 | 5,220 | 3,855,919 |
| 3 | selected_update_decode_apply | 1,656,597 | 1,322 | 6,493 | 11,161 | 3,855,919 |
| 3 | clock_pair_overhead | 10,000 | 20 | 31 | 40 | 120 |

| Run | Seconds | Million records/s | Selected updates |
|---|---:|---:|---:|
| 1 | 381.810 | 0.965 | 1,656,597 |
| 2 | 386.184 | 0.954 | 1,656,597 |
| 3 | 379.151 | 0.972 | 1,656,597 |

Medians across per-run statistics (not pooled percentiles):

| Path | p50 (ns) | p99 (ns) | p99.9 (ns) |
|---|---:|---:|---:|
| read_decode_apply | 621 | 3,166 | 5,360 |
| decode_apply | 561 | 3,086 | 5,220 |
| selected_update_decode_apply | 1,333 | 6,493 | 11,161 |
| clock_pair_overhead | 20 | 31 | 31 |

Exact nearest-rank p50/p95/p99/p99.9, means, extremes and counts were independently recalculated from the frequency JSON for every run. All runs reached exchange End of Messages and gzip EOF/CRC checks with valid final books. No clock overhead is subtracted. Startup, archive hash and warmup are before the replay timer; final invariants, histogram serialization and snapshots are after it.
