# Next Steps

## Maintenance rule

Sau mỗi lần hoàn thành một point trong roadmap, phải cập nhật lại file này ngay:

- đánh dấu point đã hoàn thành bằng `[x]`;
- giữ point chưa hoàn thành ở trạng thái `[ ]`;
- chuyển hạng mục hoàn thành lớn vào phần `Done`;
- bổ sung hoặc sắp xếp lại các bước tiếp theo nếu phạm vi đã thay đổi;
- không để roadmap lệch với trạng thái thực tế của code và tests.

## Current state

### Done

- [x] Reconstructive LOB với `ADD`, `CANCEL`, partial/full `EXECUTE`.
- [x] Price levels, FIFO trong cùng mức giá, best bid/ask và invariant checks.
- [x] CSV schema và event envelope chứa `sequence`, `instrument_id`, event type,
  order ID, side, price và quantity.
- [x] `OrderBookRegistry` route event tới nhiều instrument.
- [x] Generator và deterministic CSV replay cơ bản.
- [x] Global sequence validation phát hiện duplicate, out-of-order và gap.
- [x] Chỉ commit sequence sau khi apply thành công; retry giữ cùng sequence khi lỗi.
- [x] TCP event ingress trên loopback: binary codec, server/client, apply-result ACK,
  logging bất đồng bộ và test protocol/transport/hai process.
- [x] UDP multicast event ingress trên loopback: publisher/subscriber, dùng lại
  EventFrame 40 byte, fan-out nhiều book, bỏ gói cũ/trùng, dừng khi gap/lỗi,
  timeout và kiểm tra state so với CSV replay; async logging ở subscriber.

- [x] Local trading pipeline: matching engine, multi-client TCP order server/gateway,
  SPSC request/response/market queues, UDP publisher, subscriber/local LOB và trading app.
- [x] Price-time matching, limit/market, IOC/FOK, post-only, STP, cancel/replace,
  owner routing và ACK/fill hai phía; order manager, risk, position/PnL và maker/taker/random.
- [x] Journal đồng bộ trước ACK, deterministic restart, snapshot qua TCP, heartbeat
  và khôi phục market book sau gap/join muộn; demo tự kiểm tra và metrics JSON.

- [x] Unified PR: durable engine/risk/CI từ #16; gateway/subscriber workers + client
  SPSC queues, microprice/trade-ratio features và graceful cancellation từ #17.
- [x] Buffered suffix validation sau TCP snapshot; test half-close, 5,000-command
  burst, SIGTERM, adaptive maker và trade-driven taker qua process thật.

### Current boundary

`OrderBook::apply(ExecuteOrder)` vẫn là primitive tái dựng book; module
`order_book::trading::MatchingEngine` nhận order command riêng và tự sinh trades.
Đây là mô phỏng local hoàn chỉnh theo pipeline. Private gateway reconciliation,
engine checkpoint/rotation, real-venue adapter và production operations còn bên dưới.
Xem [trading_system.md](docs/trading_system.md) để chạy demo và đọc contracts.

## Work order

### 1. Finish multi-instrument foundation

- [x] Thêm test cho `OrderBookRegistry`:
  - cùng `order_id` trên nhiều instrument;
  - state giữa các instrument độc lập;
  - `CANCEL`/`EXECUTE` trên instrument chưa tồn tại trả `UnknownInstrument`;
  - aggregate counts và invariant validation.
- [x] Thêm `UnknownInstrument` vào `to_string(ApplyResult)`.
- [x] Hiển thị `instrument_count` trong replay summary.

### 2. Add observability

- [x] Thêm read-only `OrderBook::snapshot()` để lấy price levels và orders mà không lộ
  container nội bộ.
- [ ] Thêm `replay --verbose` để in từng event, kết quả apply và top of book.
- [ ] Thêm `--continue-on-error` để quan sát cả event thành công và thất bại.

Example:

```text
[1][instrument=1001] ADD id=1 BID 10000 x100 -> OK
best_bid=10000 best_ask=-

[2][instrument=1001] ADD id=1 BID 9999 x50 -> DUPLICATE_ORDER
best_bid=10000 best_ask=-
```

### 3. Validate sequencing

- [x] Theo dõi sequence cuối đã xử lý.
- [x] Phát hiện duplicate, out-of-order và sequence gap.
- [x] Dùng global sequence bắt đầu từ `1` và thêm test tương ứng.

### 4. Build matching engine V1

- [x] Tách inbound order command khỏi market-data `ExecuteOrder`.
- [x] Thêm `MatchingEngine::process(New)` cho limit/market command.
- [x] Tự matching với phía đối diện theo price-time priority.
- [x] Sinh trade/execution reports cho mỗi match.
- [x] Đưa remaining quantity vào book nếu order chưa khớp hết.
- [x] Test full fill, partial fill, multiple price levels và FIFO tại cùng price.

### 5. Complete order lifecycle

- [x] Replace/amend order và quy tắc giữ/mất priority.
- [x] Market order.
- [x] Time-in-force: IOC và FOK.
- [x] Post-only.
- [x] Self-trade prevention.

### 6. Add validation and risk rules

- [x] Tick size và lot size.
- [x] Price bands.
- [x] Quantity/notional limits và các pre-trade risk checks cơ bản.

### 7. Persistence and recovery

- [x] Append-only journal.
- [x] Snapshot/restore market book theo watermark và boot epoch.
- [ ] Engine checkpoint và journal rotation/compaction.
- [x] Deterministic replay sau restart.
- [x] Kiểm tra recovered state giống state trước restart.

### 8. Measure before optimizing

- [x] Predecoded in-memory benchmark.
- [x] Predecoded mixed add/reduce/match/cancel workload và kiểm tra state.
- [x] Benchmark core ns/command và p50/p95/p99/max; JSON output.
- [ ] End-to-end throughput và latency, gồm queue/network/journal.
- [ ] `perf stat`, flame graph và regression baseline.

### 9. Production pipeline

- [x] TCP protocol V1 và input validation cho Add/Cancel/Execute.
- [x] UDP multicast V1, bảo toàn ranh giới datagram và phát hiện gap/stale.
- [x] UDP boot epoch/heartbeat và gap recovery bằng TCP snapshot.
- [ ] UDP reorder window/retransmission và multicast snapshot channel.
- [x] Socket timeouts, session binding và idempotent last-request replay.
- [ ] Private gateway reconnect/reconciliation cho unobserved fills.
- [x] Reader/parser -> SPSC queue -> single-writer matching engine.
- [x] Threading, bounded queue backpressure, slow-client isolation và graceful shutdown.
- [x] Command logs và shutdown metrics JSON.
- [ ] Live monitoring, active/passive failover và operational tooling.

## Architecture progression

```text
Add/Cancel/Execute event replay
        -> multi-instrument reconstructive LOB
        -> price-time matching engine
        -> multi-instrument exchange engine
        -> recovery, risk, networking, persistence and monitoring
```
