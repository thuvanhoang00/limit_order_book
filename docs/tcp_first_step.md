# First TCP step for the LOB

This Linux/C++20 module uses the existing `MarketEvent` and its Add/Cancel/Execute
payloads. It is an event-ingress demo for the current book model. Execute events
describe external executions, as in CSV replay; this is not an exchange matching
protocol. The server applies decoded events directly to `OrderBookRegistry` on its networking
thread. This preserves single-writer ownership. An optional registry logger is
attached by the demo server; its existing SPSC worker handles log formatting and
file I/O. Networking and registry log records have the same single producer.

## Build and run

From the repository root:

```bash
cmake -S . -B build/tcp -DCMAKE_BUILD_TYPE=Debug \
  -DORDER_BOOK_BUILD_BENCHMARKS=OFF
cmake --build build/tcp --parallel
ctest --test-dir build/tcp --output-on-failure
```

Terminal 1:

```bash
./build/tcp/tcp_event_server 9000 results/tcp_server.log
```

Terminal 2:

```bash
./build/tcp/tcp_event_client data/sample_events.csv 9000 results/tcp_client.log
```

Server binds to loopback, accepts one client, applies decoded events, prints CSV
events, and exits when that client disconnects. It checks book invariants and
prints final book counts on clean disconnect. Restart it for another session
with an empty registry. Client sends one event, waits for its apply result, then
sends the next. A rejected event makes the client exit with failure. Both default to port 9000.
`ORDER_BOOK_BUILD_NETWORKING=OFF` excludes the module, tools and networking tests.

## Read the code in this order

1. `tools/tcp_event_server.cpp`: receive/decode, sequence check, registry apply, ACK.
2. `tools/tcp_event_client.cpp`: connect, encode/send, read ACK.
3. `src/net/tcp.cpp`: RAII fd ownership and exact-length I/O loops.
4. `src/net/protocol.cpp`: explicit wire encoding, validation and native types.

`send_all()` advances by the actual number of bytes accepted by the kernel.
`receive_exact()` accumulates bytes until one fixed frame is complete. It never
consumes bytes belonging to the next frame. EINTR is retried; clean EOF returns
false; EOF inside a frame throws. Errors close the session through RAII. Send uses
MSG_NOSIGNAL so a closed peer becomes an exception rather than terminating the
process through SIGPIPE. Connected sockets use TCP_NODELAY and fds use CLOEXEC.

## Wire format v1

All integer fields are unsigned, big-endian. Frames are arrays of bytes, never
raw native structs or variants. There is no length field because each direction
has one fixed frame size. Protocol changes require explicit version handling.

Event: **40 bytes**.

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 4 | Magic `LOB1` (`4c 4f 42 31`) |
| 4 | 1 | Version = 1 |
| 5 | 1 | Kind: Add = 1, Cancel = 2, Execute = 3 |
| 6 | 1 | Add side: Bid = 1, Ask = 2; otherwise 0 |
| 7 | 1 | Reserved = 0 |
| 8 | 8 | Sequence |
| 16 | 8 | Instrument ID |
| 24 | 8 | Order ID |
| 32 | 4 | Price: Add only, otherwise 0 |
| 36 | 4 | Quantity: Add/Execute, Cancel = 0 |

Apply ACK: **16 bytes**. Same magic/version, kind = 129 at byte 5, result code
at byte 6, reserved byte 7 = 0, sequence at offset 8. Explicit result codes:

| Code | ApplyResult |
| --- | --- |
| 0 | Ok |
| 1 | DuplicateOrder |
| 2 | UnknownOrder |
| 3 | InvalidPrice |
| 4 | InvalidQuantity |
| 5 | UnknownInstrument |
| 6 | QuantityExceedsRemaining |
| 7 | ParseError |
| 8 | NotImplemented |
| 9 | AllocationFailed |

The codec also retains the initial received-only ACK helper (kind 128, byte 6 = 0).
The integrated server/client use **kind 129**. Old received-only clients must be
rebuilt; the distinct kind prevents confusing receipt with successful apply.
Unknown header/kind/side/status and nonzero unused fields are rejected. A
malformed event closes the connection without an ACK.

## Book integration and session policy

The server owns one `OrderBookRegistry` for its single connection:

```cpp
if (!registry.seq_valid(event->sequence))
    throw std::runtime_error("Invalid event sequence");
const auto result = registry.apply(*event);
send_all(peer, encode_apply_ack({event->sequence, result}));
```

Sequence starts at 1 and must be consecutive globally across instruments, as in
CSV replay. Invalid sequence closes the connection before applying the event.
`seq_valid()` is read-only. Only `ApplyResult::Ok` commits the event sequence.
A rejection returns its explicit `ApplyResult` and leaves both the book and last
successful sequence unchanged. The supplied client stops on rejection. A custom
client may submit a corrected event with the same sequence on that connection;
advancing past a rejected sequence is a gap and closes the session.

Business checks (price/quantity, duplicate order ID, unknown instrument/order,
pool exhaustion) are performed by the existing registry/book. Execute events
update remaining quantity; the book does not generate trades by matching orders.

An OK apply ACK confirms the **in-memory update**, with no durability guarantee.
A disconnect before the response leaves the client outcome unknown. Do not
retry automatically; safe recovery requires session identity and deduplication.
The demo exits after one connection and does not preserve state across restarts.

For a separate book thread later, enqueue `MarketEvent` into a bounded SPSC queue,
define queue-full behavior, and use a return queue for the final apply result.
Keep exactly one producer and one consumer per queue.

## Logging

Both tools append asynchronous diagnostic logs. The optional final argument selects
its path; defaults are `tcp_server.log` and `tcp_client.log`. Parent directories
must exist. Each process needs its own path. Invalid log paths fail before the
server listens or the client connects.

Server logs include listen/connect/disconnect/finish at INFO, every attempted
book operation through `OrderBookRegistry`, sent ACKs, and protocol/sequence/I/O
failures at ERROR. Client logs connections, received ACKs (including rejected
results), CSV errors and network failures. Records include sequence, instrument,
port and, for system exceptions, numeric `error_code`. Detailed exception messages
remain on stderr. Successful ACKs are DEBUG; rejected ACKs are WARN.

Both processes stop producing, drain their logger on exit, and report
`log_dropped`, `log_format_failures`, and `log_write_failed` on stderr. Formatting
or write errors return failure. Queue-full drops are counted and do not block
networking or fail the session. These are best-effort diagnostics; a successful
ACK confirms the book update and can precede a later diagnostic write failure.

## Deliberate limits

Blocking socket operations can wait indefinitely for a slow/stalled peer. This
first step has no deadlines, reconnect, authentication, recovery or multi-client
event loop. Stdout and stop-and-wait ACKs make it a correctness/learning demo,
not a latency benchmark. The functions require blocking sockets: EAGAIN is an
error, not a readiness signal to retry in a busy loop.

For non-blocking I/O, move receive/send offsets and pending bytes into per-session
state, then resume on readiness (e.g. epoll). Keep the codec and native event
types. UDP multicast is a separate next step with packet sequence/gap handling.

## Validation

Networking tests cover an independent byte fixture, all native event variants,
full-width integer values, malformed headers/unused fields, stream chunking,
truncated EOF, large transfers with a small send buffer, closed-peer sends, fd
move ownership and a real loopback TCP event/ACK exchange. Stream tests use local
socket pairs; loopback tests cover listen/connect/accept. They check correctness,
not latency or exhaustive syscall fault injection.

Two-process tests also compare the sample workload against CSV replay, verify
corrected retries after rejected updates, malformed/truncated frames, invalid
sequences, mismatched/missing ACKs, connection errors and log write failures.
No latency measurement was performed.
