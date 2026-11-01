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

`limited:true` and `complete_input:false` mean only the requested prefix was read.
Full input requires EOF plus ITCH End of Messages or End of Session for every
observed PITCH unit. These markers/framing checks cannot prove that an ITCH archive
retained every original message, because archive payloads have no transport sequence.
Missing symbols and invalid streams return a nonzero exit without a success JSON.
Summary and snapshot paths must differ from each other and from the input.

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

Debug and ASan/UBSan full CTest suites passed 156/156 each. Added tests cover endian
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
This is a prefix check, not a completed-day or Cboe capture validation. Local evidence
is retained in `results/itch-aapl-10m*.json`,
`results/itch-aapl-independent-verification.json` and `results/verify_itch_prefix.py`.
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
