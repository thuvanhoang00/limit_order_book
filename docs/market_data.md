# Native market-data parsers

`order_book::feed` decodes native exchange binary messages into shared events and
reconstructs displayed order books using the existing `OrderBook`. CSV conversion
is not involved. The existing CSV/demo codecs remain available for older tools.

| Adapter | Venue selection | Binary input |
|---|---|---|
| Nasdaq TotalView ITCH 5.0 | `nasdaq` | Big-endian two-byte length followed by one ITCH payload, as in Nasdaq historical `.gz` archives |
| Cboe U.S. equities Multicast PITCH 2.x | `cboe-bzx`, `cboe-byx`, `cboe-edga`, `cboe-edgx` | Concatenated native Sequenced Unit Header packets, each with its own little-endian two-byte total length |

The PITCH adapter covers the equities order-message subset below. Its tests use
specification-based binary fixtures; no captured Cboe trading day has been verified.
HOSE/HNX, index feeds, NYSE, PITCH TCP/ASCII, options, PCAP/Ethernet/IP extraction,
live socket ingestion, spin servers and retransmission are not implemented here.
Unknown message types fail explicitly rather than silently producing an incomplete
book. In particular, additional Cboe auction/control messages and newer protocol
extensions require a matching decoder before replaying captures containing them.

## Build and replay

Install zlib development headers (`zlib1g-dev` on Ubuntu), then:

```sh
cmake --preset release
cmake --build --preset release --parallel 4 --target replay_market_data
mkdir -p results

# Full archive: stream gzip directly, filtering the reconstructed book to AAPL.
./build/release/replay_market_data \
  --input data/12302019.NASDAQ_ITCH50.gz --format itch50 --symbol AAPL \
  --pool-bytes 8388608 --output results/aapl.json --snapshot results/aapl-book.json

# Fast prefix check from the start of the archive, explicitly marked incomplete.
./build/release/replay_market_data \
  --input data/12302019.NASDAQ_ITCH50.gz --symbol AAPL --max-records 10000000

# Extracted U.S. equities PITCH packet stream; not a .pcap file.
./build/release/replay_market_data \
  --input data/bzx.pitch.gz --format pitch --venue cboe-bzx --symbol AAPL
```

Plain binary and gzip inputs share a buffered streaming reader; no decompressed
file or entire-day message vector is created. Gzip members read to EOF are checked
by zlib, including truncation/CRC errors. As with `gzread`, non-gzip trailing garbage
after gzip members is ignored. A record limit stops before EOF and does not verify
the rest of the compressed file. `--max-records` counts ITCH messages or PITCH
packets, respectively; PITCH `messages` counts individual payloads within packets.

Summary JSON includes messages, selected-book updates, trade notices across the
input, wall time and achieved messages/s (including input, decompression, decoding
and apply), final BBO/order counts, and invariant validation. Snapshot JSON retains
price levels and individual orders in FIFO order. Final invariant checks, snapshot
construction and JSON output are outside the replay timer. These timings are
offline reconstruction throughput, not order-entry or network latency.

## Full-archive latency measurement

```sh
python3 tools/run_feed_benchmarks.py \
  --input data/01302019.NASDAQ_ITCH50.gz --symbol AAPL --cpu 2
# One replay with exact frequency output:
./build/release/replay_market_data \
  --input data/01302019.NASDAQ_ITCH50.gz --symbol AAPL --pool-bytes 8388608 \
  --latency 1 --output results/day.json --histogram results/day.histogram.json
```

The runner defaults to three complete Release replays, an untimed one-million-record
prefix warmup, and an 8 MiB selected-book pool. It hashes the input, freezes the
binary/source identities and records affinity/build metadata. It does not flush
the OS file cache or change CPU frequency settings. All records in each measured
replay are timed, including the initial and final exchange messages; there is no
sampling or warmup exclusion within the measured file.

`--latency 1` adds exact per-input-record nanosecond frequencies and nearest-rank
p50/p95/p99/p99.9, mean, min and max. There are three paths:

- `read_decode_apply`: before the record read through completion of decode/apply.
  Includes buffered file access, gzip refill/decompression, framing, decoding,
  routing and selected LOB application. Refill costs occur on particular records,
  so most read calls consume already-buffered bytes.
- `decode_apply`: after the complete record is read through decode/apply. Includes
  routing updates for symbols other than the selected symbol.
- `selected_update_decode_apply`: the same decode/apply interval, restricted to
  records which actually changed a selected displayed book. For ITCH one record
  is one message; for PITCH it is one packet and can contain several mutations.

These are offline service times while replaying as fast as possible. They do not
use the ITCH exchange timestamp as a local latency clock, reproduce its original
arrival rate, or measure network/order-entry matching latency. The pre-existing
TCP/UDP pipeline benchmark measures a separate workload.

Three steady-clock reads per timed record and frequency collection add overhead.
`clock_pair_overhead` reports 10,000 consecutive clock-pair durations collected
before replay; it is not subtracted from the results. Histogram recording is
outside each service-time interval but inside wall time/achieved throughput.
Dense exact bins cover 0..100 microseconds; larger durations use sparse exact
frequency counts. Memory depends on distinct outlier durations rather than
retaining every individual sample. Histogram JSON contains sorted
`[nanoseconds, count]` pairs, enabling independent percentile recomputation.

The runner refuses incomplete sessions, checks gzip EOF/CRC and final book
invariants, recalculates every histogram statistic, and compares final snapshots
and counts across repetitions. An existing evidence directory cannot be reused.
Statistics and histograms are written only after replay and validation succeed.

`limited:true` and `complete_input:false` mean only the requested prefix was read.
Full input requires EOF plus ITCH End of Messages or End of Session for every
observed PITCH unit. These markers/framing checks cannot prove that an ITCH archive
retained every original message, because archive payloads have no transport sequence.
Missing symbols and invalid streams return a nonzero exit without a success JSON.
Summary and snapshot paths must differ from each other and from the input.

## Observer overhead and slow-record diagnostics

```sh
# Complete off/on pairs, alternating their order. GNU time records process resources.
python3 tools/run_feed_benchmarks.py \
  --input data/01302019.NASDAQ_ITCH50.gz --symbol AAPL --cpu 2 \
  --repetitions 2 --compare-instrumentation

# A separate diagnostic run: do not mix it into the off/on overhead comparison.
./build/release/replay_market_data \
  --input data/01302019.NASDAQ_ITCH50.gz --symbol AAPL --pool-bytes 8388608 \
  --latency 0 --outlier-trace results/outliers.json \
  --outlier-threshold-ns 100000 --outlier-capacity 256
```

The paired runner uses the same frozen executable and archive for both flags.
`--latency 0` retains only aggregate replay wall timing; `--latency 1` adds the
per-record clocks/histograms. Pair order is off/on, then on/off, repeated as needed.
Time overhead is `on_seconds / off_seconds - 1`; throughput loss is
`1 - on_rate / off_rate`. These are different percentages. The result estimates
the cost of enabling this observer, not a correction to subtract from percentile
values. `/usr/bin/time` is required for comparison mode; its CPU time, peak RSS,
context switches and fault counters cover the entire child process, including
final validation/output. The replay wall timer excludes that final work.

The diagnostic requires ITCH, one symbol and latency histograms disabled. It learns
stock-locate mappings from successfully decoded Directory events and selects only
`A/F/E/C/X/D/U` records for that symbol. There is no hardcoded daily stock locate.
Every selected update gets thread CPU and resource observations; only records
above the threshold are classified, and at most the slowest configured number are
retained. Record ordinal, exchange timestamp, type and order ID identify the input.

The observation order is resource counters → thread CPU → wall start → decode/apply
→ wall end → thread CPU → resource counters. CPU/resource windows are slightly
wider than wall time and have their own overhead. `estimated_off_cpu_ns` clamps
`wall - CPU` at zero; it is evidence of elapsed time absent from the thread's CPU
accounting, not a precise scheduler trace. Context-switch/fault deltas are associated
with that wider window. Slow records with a switch and at least half their wall
time absent from CPU accounting are counted separately; CPU-dominant means CPU
time is at least 80% of wall time. Fault counts may overlap those categories.

Use a separate `perf record` CPU-sampling run when supported to identify CPU
hotspots. Sampling, CPU clocks and resource probes perturb execution: diagnostic
throughput/latency is not substituted for the ordinary on/off measurements.
New diagnostics explain newly observed slow records, not the exact historical
record that produced a previous run's maximum (old histograms have no record IDs).
No global scheduler, CPU-power or perf-permission settings are changed.

The [recorded on/off and diagnostic report](../benchmarks/results/20261101-itch-observer-overhead/report.html)
contains two complete control pairs plus a separate full-archive perf/outlier run.
It uses the [standalone HTML convention](reporting.md). Debug and ASan/UBSan suites
passed 159/159 each after adding these diagnostics; Python report tests passed 9/9.

## Normalized events and reconstruction

- ITCH `R` establishes the daily stock-locate mapping. `A/F` add displayed orders;
  `E/C` execute shares; `X` removes a partial quantity; `D` deletes; `U` replaces
  the ID/price/quantity and loses FIFO priority. `P/Q` are separate trade notices
  and never reduce a displayed order a second time. `B` never restores orders.
- PITCH long/short/expanded adds (`21/22/2F` hex), execution (`23/24`), reductions
  (`25/26`), modifications (`27/28`), delete (`29`), and trade notices (`2A/2B/30`)
  share the event contract. Time (`20`), Unit Clear (`97`), Trade Break (`2C`),
  End of Session (`2D`) and Trading Status (`31`) are recognized. Expanded equities
  Add lengths 40/41 and Time lengths 6/10 are accepted; options layouts fail.
- PITCH `24` uses the explicit remaining displayed quantity, including hidden-size
  executions where executed size exceeds the previous displayed size. Execution
  price does not reprice the resting order. Modification's Maintain Priority bit
  controls FIFO; a no-op modification without that bit still moves to the tail.
- Prices normalize to integer units of 1/10,000; PITCH short prices are multiplied
  by 100. Timestamps are exchange-local nanoseconds since midnight, not UTC or
  measured latency. This adapter does not normalize trading dates/time zones.
- Book keys are `(venue, channel/unit, symbol)`; order references are namespaced
  by `(venue, channel/unit, order ID)`. Separate venues/units may reuse an ID.
  Venue identity is provided by capture configuration, not inferred from a symbol.

Use one decoder per source/session and preserve its event order. ITCH's `sequence`
is the input record ordinal, not a Nasdaq network sequence. PITCH verifies its
header count/length, clocks and consecutive sequence independently per unit,
starting at sequence 1; it validates a whole packet before exposing its events.
Mid-session captures, duplicate packets and gaps fail; use a complete deduplicated
capture from session start. Sequence rollover requires restarting the session.
Decoder errors poison that decoder. Book apply errors poison the reconstruction;
restart from known history rather than skipping rejected events. The books object
is a sink for successfully decoded events; callers must stop using it if upstream
decoding/transport fails. It is single-writer.

Filtering reconstructs only selected symbols but retains routing/quantity state
for other live orders, since PITCH modifications have no symbol. Memory therefore
still depends on all active order references. Start with one symbol before trying
a whole-market replay. Selected-book prices/quantities outside the core's unsigned
32-bit range are rejected explicitly; routing metadata retains wider prices for
unselected symbols. This implementation does not infer hidden liquidity,
recalculate corrected trade statistics, or prove exchange matching decisions.

Each reconstructed book defaults to a 1 MiB order-node pool; `--pool-bytes` changes
that fixed construction-time byte budget. Price-level maps, indexes and routing
metadata still allocate normally. The core's existing default pool remains 16 KiB.
A slot's size depends on the platform's `std::list` node. Exhaustion stops replay;
restart with a larger budget. Core feed replacement reserves its new node/map/index
storage before removing the old order, and rolls back on allocation failure.

## Verification

Debug and ASan/UBSan full CTest suites passed 158/158 each after adding the latency
histogram tests (the initial native-feed change passed 156/156). Added tests cover endian
and fixed-point conversion, native raw/gzip replay, malformed framing, CRC/torn
input, unknown types/references, hidden-size executions, partial cancels, replacement
IDs/FIFO flags, namespace isolation, filtering, session completion and independently
injected allocation failures during replacement.

A Release prefix replay of the locally downloading ITCH archive processed
10,000,000 messages, including 24,845 AAPL book updates. Final AAPL state had 1,131
orders; an independent Python reconstruction matched every price level, quantity,
order ID and FIFO position (peak 1,132 orders). SHA-256 of the decoded, length-prefixed
input prefix was
`efdee1d09985f03297f07a5cb294ac4308484c14b4a77ae323518a31cb9db246`.
This is a prefix check, not a completed-day or Cboe capture validation. The replay,
FIFO snapshot, independent verification and Python oracle are archived in
[prefix-verification](../benchmarks/results/20261101-full-itch-latency/prefix-verification/).
The temporary download filename does not establish the archive's trading date.

## References and another adapter

Implement a separate decoder for another feed/version, map its semantics to
`feed::Event`, and test native byte fixtures plus an independent expected book.
Do not map price-level snapshots to invented order IDs/FIFO positions. If the new
feed is aggregated by price, give it a separate price-level book contract.

- [Nasdaq ITCH 5.0 specification](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf)
- [Nasdaq historical archives](https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/)
- [Cboe U.S. Equities/Options Multicast PITCH specification](https://www.cboe.com/document/tech-spec/document/technical-specifications/cboe-titanium-u.s.-equitiesoptions-multicast-pitch-specification)
- [Cboe execution-at-price semantics](https://www.cboe.com/document/tech-spec/content/technical-specifications/cboe-titanium-u.s.-equitiesoptions-multicast-pitch-specification/pitch-2.x-messages/order-modification-messages/order-executed-at-pricesize)

## Recorded complete-day result

The [2019-01-30 full-archive measurement](../benchmarks/results/20261101-full-itch-latency/README.md)
processed 368,366,634 messages and 1,656,597 AAPL updates
in each of three complete replays. EOF/CRC, end-of-session, final invariants,
exact histogram recomputation and repeated snapshots all passed.
Median instrumented throughput was 0.965 million messages/s.

| Path | p50 (µs) | p99 (µs) | p99.9 (µs) |
|---|---:|---:|---:|
| read_decode_apply | 0.621 | 3.166 | 5.360 |
| decode_apply | 0.561 | 3.086 | 5.220 |
| selected_update_decode_apply | 1.333 | 6.493 | 11.161 |

Values are medians across three per-run statistics on CPU 2 with the existing
`powersave` governor. Clock-pair median overhead was 20 ns; no subtraction
is applied. Debug and ASan/UBSan full suites passed 158/158 each after adding latency
instrumentation. See the report for exact input identity, every repetition, raw
frequency counts and the offline/buffered measurement boundaries.
