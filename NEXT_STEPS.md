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

### Important limitation

`ExecuteOrder` hiện chỉ thông báo rằng một order đã khớp một quantity rồi cập nhật
book. Hệ thống chưa tự tìm order đối ứng, chưa matching và chưa sinh trade.

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

- [ ] Thêm read-only `OrderBook::snapshot()` để lấy price levels và orders mà không lộ
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

- [ ] Tách inbound order command khỏi market-data `ExecuteOrder`.
- [ ] Thêm `submit(LimitOrder)`.
- [ ] Tự matching với phía đối diện theo price-time priority.
- [ ] Sinh trade/execution reports cho mỗi match.
- [ ] Đưa remaining quantity vào book nếu order chưa khớp hết.
- [ ] Test full fill, partial fill, multiple price levels và FIFO tại cùng price.

### 5. Complete order lifecycle

- [ ] Replace/amend order và quy tắc giữ/mất priority.
- [ ] Market order.
- [ ] Time-in-force: IOC và FOK.
- [ ] Post-only.
- [ ] Self-trade prevention.

### 6. Add validation and risk rules

- [ ] Tick size và lot size.
- [ ] Price bands.
- [ ] Quantity/notional limits và các pre-trade risk checks cơ bản.

### 7. Persistence and recovery

- [ ] Append-only journal.
- [ ] Snapshot và restore.
- [ ] Deterministic replay sau restart.
- [ ] Kiểm tra recovered state giống state trước restart.

### 8. Measure before optimizing

- [ ] Predecoded in-memory benchmark.
- [ ] Mixed add/cancel/execute/match workload với fixed seed.
- [ ] Theo dõi throughput, ns/event và p50/p95/p99 latency.
- [ ] `perf stat`, flame graph và regression baseline.

### 9. Production pipeline

- [ ] Network protocol và input validation.
- [ ] Reader/parser -> queue -> single-writer matching engine.
- [ ] Threading, backpressure và graceful shutdown.
- [ ] Monitoring, metrics và operational recovery.

## Architecture progression

```text
Add/Cancel/Execute event replay
        -> multi-instrument reconstructive LOB
        -> price-time matching engine
        -> multi-instrument exchange engine
        -> recovery, risk, networking, persistence and monitoring
```
