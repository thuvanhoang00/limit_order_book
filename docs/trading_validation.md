# Trading implementation verification

Verified on Linux with GCC 11.4.0, C++20, on 2026-10-31.

| Check | Result |
|---|---|
| Debug full `check` target | 137/137 CTest tests passed |
| ASan + UBSan full `check` target | 137/137 CTest tests passed |
| Networking and tools disabled | 115/115 CTest tests passed |
| Existing Python benchmark-report tests | 6/6 passed |
| Release exchange/maker/taker demo | All five scenario checks passed |
| Release matching benchmark smoke | 8,000 commands, 1,000 trades, valid final state; JSON and raw latency CSV produced |
| ThreadSanitizer | Binaries built, execution unavailable on this host (see below) |

The full CTest count includes 23 new core/protocol/recovery/strategy tests and one
Python process-test entry with eleven scenarios. Existing book, memory, logger,
TCP, UDP and allocation coverage remains enabled. The randomized matching test
compares every fill in a seeded 2,000-order workload against an independent
price-time model.

The process scenarios verify fragmented/coalesced TCP input, unsolicited private
maker fills, last-request idempotence, client identity binding, a slow partial
client, multicast fan-out, heartbeat watermark, durable restart, C++ client
positions, snapshot recovery after a malformed datagram, concurrent clients and
acknowledged-command persistence, configured tick/lot rules actual maker/taker strategy runs and snapshots of both full default books.

The release demo produced two trades of total quantity seven, with maker/taker
positions -1/+1, cash +107/-107 and equal market sequence 6. Both books were
synchronized; journal replay produced the same sequence and trade count. The demo
writes full evidence under its printed `results/trading-<timestamp>/` directory.
Benchmark smoke results are validation only: concurrent compilation was active;
these numbers are not an isolated performance baseline or network latency result.

The TSan executable could not start because the host runtime reported
`FATAL: ThreadSanitizer: unexpected memory mapping`. No successful TSan run or
absence-of-races claim is made. ASan/UBSan do not replace a race detector. The
`tsan` preset remains available for a compatible host. The GitHub Actions workflow
runs Debug/ASan tests and demos; its remote results are separate from these local
checks.

Reproduce using the commands in [the system guide](trading_system.md). `git diff
--check` also passed. No real venue, physical-LAN, failover or production-load test
was performed.
