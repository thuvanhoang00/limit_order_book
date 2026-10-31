# UDP multicast event ingress

This adds a multicast publisher/subscriber to the existing Linux C++20 LOB,
TCP transport, codec and async logger. Socket fd ownership is extracted from
`tcp.hpp`/`tcp.cpp` into `socket.hpp`/`socket.cpp`; including `tcp.hpp` still exposes
the same `Socket` API. TCP behavior and event wire format are unchanged.

## Read the flow

1. `tools/udp_event_publisher.cpp`: CSV -> native event -> existing 40-byte codec
   -> one `sendto()` per event.
2. `tools/udp_event_subscriber.cpp`: `recvmsg()` -> length/truncation checks ->
   decode -> sequence policy -> registry apply -> business operation log.
3. `src/net/udp.cpp`: socket options, bind, membership, send and receive semantics.
4. `tests/net/test_udp.cpp` and `test_udp_cli.py`: datagram boundaries and real
   multicast processes, including comparison against existing CSV replay.

The subscriber owns its registry on the network thread. Its logger has one
producer and one worker consumer, as in TCP ingress. No networking-to-book queue
has been added. This is reconstructive market-data replay: Execute events describe
external executions, and the book does not generate trades by matching orders.

## Command line

```text
udp_event_publisher <events.csv> [port=9001] [group=239.255.0.1] [interface=127.0.0.1] [pace_us=1000]
udp_event_subscriber <event_count> [port=9001] [group=239.255.0.1] [interface=127.0.0.1] [timeout_ms=30000] [log_file=udp_subscriber.log]
```

Arguments are positional. `event_count` is the number of successfully applied
events, excluding stale/duplicate packets. Port must be 1..65535 in tools;
publisher pacing 0..1,000,000 microseconds; receive timeout 1..60,000 milliseconds.
No DNS is used. Interface means the IPv4 address of a local NIC, not the group
address or NIC name. The default loopback address makes same-host tests explicit.

For a LAN experiment choose the same multicast group/port on both hosts, and pass
each host's own NIC IPv4 address. LAN multicast additionally depends on network
and firewall configuration. This delivery is verified on loopback only.

## Socket setup

Publisher: `SOCK_DGRAM | SOCK_CLOEXEC`, outgoing `IP_MULTICAST_IF`,
`IP_MULTICAST_LOOP=1`, `IP_MULTICAST_TTL=1`, and bind to the chosen source interface
with an ephemeral port. There is no `listen()`/`accept()`/TCP handshake.

Subscriber: `SO_REUSEADDR` permits several subscribers on the same group/port;
Linux group-specific `bind(group, port)` isolates unicast and other group addresses;
`IP_MULTICAST_ALL=0` limits delivery to this socket's memberships;
`IP_ADD_MEMBERSHIP` joins on the explicit interface. `SO_RCVTIMEO` bounds each
blocking receive. Closing the RAII socket also drops its membership.

Neither helper changes the NIC, routing table or system-wide socket settings.

## Datagram contract

- One send is one complete datagram. Send retries only EINTR. A short result is
  an error; never send a remainder as another datagram. Successful send only means
  the local kernel accepted it, with no receipt/apply guarantee.
- One receive consumes one datagram. It never combines multiple packets, even if
  two packets have lengths 3 and 37. Each is individually too short for an event.
- `Datagram.size` reports copied bytes. `truncated` comes from `MSG_TRUNC`; an
  oversized packet with a valid 40-byte prefix is rejected, and the tail is
  discarded rather than read on the next call.
- Zero-byte datagrams are valid at the UDP layer, not EOF; this event protocol
  rejects them for length. Timeout returns `nullopt`; transport errors throw.
- Timeout applies per recvmsg syscall. EINTR retries can restart that timeout;
  it is not an absolute deadline for a frame or the whole session.

The 40-byte event format is documented in [tcp_first_step.md](tcp_first_step.md).
It is reused without a second UDP-specific serialization format. No ACK datagrams
are sent. Normal frames are small enough to avoid ordinary Ethernet IP fragmentation.

## Sequencing and correctness

Single trusted publisher and one global sequence stream per group/port. New book
starts at sequence 1. `seq <= last_applied` is stale/duplicate and is dropped. This
does not authenticate packets or compare payloads of duplicate sequences.

`seq > last_applied + 1` is a gap. The subscriber marks the run unsynchronized
and stops before apply. A future packet might be reordered or follow a loss; this
implementation treats both as requiring recovery, with no reorder window. An
old packet after a gap does not silently heal the book. Apply rejection leaves
the last successful registry sequence unchanged and stops this feed.

Successful completion means the requested finite prefix was applied and book
invariants passed. `book_synchronized=1` describes that prefix only. Count is
known out of band in this demo: a missing tail leads to timeout before count is
reached. A live feed needs heartbeat/session metadata to detect tail loss or a
publisher restart; sequence alone cannot detect loss when nothing later arrives.
There is no UDP EOF, snapshot, retransmission or persistent state across restarts.
Joining a running feed without its initial history cannot construct a valid book.

On errors, stderr reports last successful sequence/counts and the process exits
nonzero. Async logs record join/finish, old packets, gap, protocol/transport/
timeout errors and rejected applies. Logging write/format errors also produce
nonzero exit status; `book_synchronized` specifically describes feed processing.

Pacing, stdout CSV and diagnostic logging are for learning/correctness, not a
latency benchmark. Kernel receive-buffer overflow can still lose datagrams.

## References

- [Linux ip(7)](https://man7.org/linux/man-pages/man7/ip.7.html): multicast options.
- [Linux udp(7)](https://man7.org/linux/man-pages/man7/udp.7.html): datagram semantics.
- [Linux recv(2)](https://man7.org/linux/man-pages/man2/recvmsg.2.html): MSG_TRUNC,
  timeout errors and zero-byte datagrams.
