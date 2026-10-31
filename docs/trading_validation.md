# Unified trading implementation verification

Verified on Linux with GCC 11.4.0 and C++20 on 2026-10-31. The implementation
retains the durable exchange/risk/protocol from PR #16 and integrates queued
client workers, market features, transport stress coverage and coordinated exit
from PR #17.

| Check | Result |
|---|---|
| Debug full CTest suite | 141/141 passed |
| ASan + UBSan full CTest suite | 141/141 passed |
| Networking and tools disabled | 119/119 passed |
| Existing Python benchmark-report tests | 6/6 passed |
| Debug and Release exchange/maker/taker/observer demo | All six checks passed |
| Release matching benchmark smoke | 8,000 commands, 1,000 trades, valid final state; JSON and raw latency CSV |
| ThreadSanitizer | Not executed for the unified build; this host's prior TSan startup failed with `unexpected memory mapping` |

The full suite includes the original core, memory, logger, CSV, TCP, UDP and
allocation tests. Matching is checked against an independent 2,000-order seeded
price-time model. Four feature tests verify weighted microprice, missing quotes,
independent/reset state, wide quantities and prices, and pre-Execute depth as the
trade-ratio denominator.

The process-test entry contains sixteen scenarios. These cover fragmented and
coalesced TCP input, unsolicited private fills, last-request idempotence, identity
binding, stalled clients, multicast fan-out, heartbeat watermarks, journal restart,
malformed feed recovery, configured rules, full default books and actual trading
apps. New scenarios exercise a 5,000-command add/cancel FIFO burst, TCP half-close,
SIGTERM cancellation with worker joins, adaptive microprice maker quotes and a
trade-ratio taker following a signal once.

The finite demo uses real gateway/subscriber workers and saves process output,
journal, exchange metrics, replay output and a summary under `results/trading-*`.
It verifies two trades totaling seven units, conserved cash/positions, valid
client snapshot watermarks, zero remaining client orders, an empty final observer
book and deterministic journal replay. Clients exit at different valid watermarks
because cancel-on-exit emits market updates; the final observer runs after both
clients finish and checks the exchange's final sequence.

The benchmark is a smoke validation, not an isolated performance baseline.
ASan/UBSan do not replace a race detector. The `tsan` preset remains available for
a compatible host. The GitHub Actions workflow runs Debug/ASan suites, reporting
tests and the demo; remote results are independent from the local runs above.

Reproduce using [the system guide](trading_system.md). `git diff --check` passes.
No real venue, physical-LAN, failover or production-load test was performed.
