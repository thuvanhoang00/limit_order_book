#!/usr/bin/env python3
"""Real exchange/client processes and independently encoded TCP/multicast frames."""
import argparse
import os
import pathlib
import re
import select
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest

REQUEST = struct.Struct('!4sBBBBBB6xQQQQII8x')
REPORT = struct.Struct('!4sBBBBB7xQQQQQQQIII4xQ')
MARKET = struct.Struct('!4sBBBBQQQQQQQIIQ')
GROUP = '239.255.42.1'
BIN = None


def command(client=1, sequence=1, order=1, side=1, price=99, quantity=5,
            instrument=1, kind=1, flags=0, tif=0):
    return REQUEST.pack(b'LBT2', 1, 1, kind, side, flags, tif,
                        client, sequence, instrument, order, price, quantity)


def read_exact(peer, size):
    result = bytearray()
    while len(result) < size:
        data = peer.recv(size - len(result))
        if not data:
            raise RuntimeError('Unexpected peer EOF')
        result.extend(data)
    return bytes(result)


def read_line(stream, timeout=5):
    end = time.monotonic() + timeout
    result = bytearray()
    while time.monotonic() < end:
        if not select.select([stream], [], [], max(0, end - time.monotonic()))[0]:
            break
        byte = os.read(stream.fileno(), 1)
        if not byte:
            break
        result.extend(byte)
        if byte == b'\n':
            return result.decode()
    raise RuntimeError(f'Expected process output, got {result!r}')


def udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserved:
        reserved.bind(('127.0.0.1', 0))
        return reserved.getsockname()[1]


class TradingSystem(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.temp = pathlib.Path(self.directory.name)
        self.processes = []
        self.peers = []
        self.response_sequences = {}
        self.inc = udp_port()
        self.snap = udp_port()
        while self.inc == self.snap:
            self.snap = udp_port()

    def tearDown(self):
        for peer in self.peers:
            peer.close()
        for process in self.processes:
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)
            process.stdout.close()
            process.stderr.close()
        self.directory.cleanup()

    def process(self, name, *args):
        process = subprocess.Popen([str(BIN / name), *map(str, args)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        self.processes.append(process)
        return process

    def server(self, *extra):
        server = self.process('exchange_server', '--tcp-port', 0,
                              '--incremental-port', self.inc, '--snapshot-port', self.snap,
                              '--snapshot-ms', 60, '--instruments', '1,2', *extra)
        ready = read_line(server.stderr)
        self.assertIn('Ready tcp_port=', ready)
        port = int(re.search(r'tcp_port=(\d+)', ready)[1])
        return server, port

    def peer(self, port):
        peer = socket.create_connection(('127.0.0.1', port), timeout=3)
        peer.settimeout(3)
        self.peers.append(peer)
        self.response_sequences[peer.fileno()] = 0
        return peer

    def response(self, peer):
        values = REPORT.unpack(read_exact(peer, REPORT.size))
        self.assertEqual(values[:3], (b'LBT2', 1, 2))
        self.response_sequences[peer.fileno()] += 1
        self.assertEqual(values[6], self.response_sequences[peer.fileno()])
        return dict(zip(('magic', 'version', 'kind', 'type', 'side', 'status',
                         'sequence', 'request', 'client', 'instrument', 'order', 'market',
                         'trade', 'price', 'quantity', 'leaves', 'session'), values))

    def exchange(self, peer, packet):
        peer.sendall(packet)
        result = []
        while not result or result[-1]['type'] != 6:
            result.append(self.response(peer))
        return result

    def stop_server(self, process):
        process.send_signal(signal.SIGTERM)
        out, err = process.communicate(timeout=6)
        self.assertEqual(process.returncode, 0, err.decode())
        return out.decode()

    def client(self, port, client, text='', run_ms=500, **options):
        path = self.temp / f'{client}.orders'
        path.write_text(text)
        args = ['--tcp-port', port, '--client-id', client, '--incremental-port', self.inc,
                '--snapshot-port', self.snap, '--orders', path, '--run-ms', run_ms,
                '--interval-ms', 10]
        for key, value in options.items():
            args.extend(['--' + key.replace('_', '-'), value])
        process = self.process('trading_client', *args)
        self.assertIn('Trading client ready', read_line(process.stderr))
        return process

    def test_multi_client_fills_owned_cancels_and_independent_instruments(self):
        server, port = self.server()
        seller, buyer = self.peer(port), self.peer(port)
        first = self.exchange(seller, command(side=2, price=101, quantity=10))
        self.assertEqual(first[0]['type'], 1)
        fills = self.exchange(buyer, command(client=2, price=102, quantity=7))
        self.assertEqual([r['type'] for r in fills], [1, 3, 6])
        passive = self.response(seller)
        self.assertEqual((passive['type'], passive['request'], passive['leaves']), (3, 0, 3))
        self.assertEqual((fills[1]['price'], fills[1]['quantity']), (101, 7))
        self.assertEqual(passive['trade'], fills[1]['trade'])
        other = self.exchange(buyer, command(client=2, sequence=2, instrument=2, price=99))
        self.assertEqual(other[0]['type'], 1)
        rejected = self.exchange(buyer, command(client=2, sequence=3, kind=2, price=0, quantity=0))
        self.assertEqual((rejected[0]['type'], rejected[0]['status']), (2, 4))
        canceled = self.exchange(seller, command(sequence=2, kind=2, price=0, quantity=0))
        self.assertEqual(canceled[0]['type'], 4)
        out = self.stop_server(server)
        self.assertIn('requests=5 ', out)
        self.assertIn('unroutable=0', out)

    def test_fragmented_and_coalesced_tcp_frames_and_rejected_sequence_consumption(self):
        server, port = self.server()
        peer = self.peer(port)
        one = command(quantity=0)
        peer.sendall(one[:11])
        peer.sendall(one[11:] + command(sequence=2, order=2))
        reports = [self.response(peer) for _ in range(4)]
        self.assertEqual([r['type'] for r in reports], [2, 6, 1, 6])
        self.assertEqual(reports[0]['status'], 6)
        self.assertEqual(reports[2]['request'], 2)
        self.stop_server(server)

    def test_tcp_half_close_still_drains_accepted_command_reports(self):
        server, port = self.server()
        peer = self.peer(port)
        peer.sendall(command())
        peer.shutdown(socket.SHUT_WR)
        self.assertEqual(self.response(peer)['type'], 1)
        self.assertEqual(self.response(peer)['type'], 6)
        self.assertEqual(peer.recv(1), b'')
        self.stop_server(server)

    def test_async_logs_have_separate_producers_and_drain_on_shutdown(self):
        prefix = self.temp / 'exchange'
        server, port = self.server('--log-prefix', prefix)
        client_log = self.temp / 'trading.log'
        client = self.client(port, 1, 'NEW 1 1 BID 99 5 GTC\n', run_ms=150, log=client_log)
        out, err = client.communicate(timeout=5)
        self.assertEqual(client.returncode, 0, err.decode())
        self.assertIn(b'log_dropped=0', out)
        summary = self.stop_server(server)
        self.assertIn('log_dropped=0', summary)
        self.assertIn('ExchangeRequest', pathlib.Path(str(prefix) + '.matcher.log').read_text())
        self.assertIn('ExchangeReport', pathlib.Path(str(prefix) + '.matcher.log').read_text())
        self.assertIn('ExchangeMarket', pathlib.Path(str(prefix) + '.publisher.log').read_text())
        self.assertIn('TradingRequest', client_log.read_text())
        self.assertIn('TradingReport', client_log.read_text())

    def test_trading_log_write_failure_is_reported_after_orders_are_drained(self):
        server, port = self.server()
        client = self.client(port, 1, 'NEW 1 1 BID 99 5 GTC\n', run_ms=150, log='/dev/full')
        out, err = client.communicate(timeout=5)
        self.assertEqual(client.returncode, 1)
        self.assertIn(b'active_orders=0', out)
        self.assertIn(b'asynchronous log write/format failure', err)
        self.stop_server(server)

    def test_duplicate_identity_and_bad_sequence_disconnect_only_offender(self):
        server, port = self.server()
        good = self.peer(port)
        self.exchange(good, command())
        duplicate = self.peer(port)
        duplicate.sendall(command(order=2))
        self.assertEqual(duplicate.recv(1), b'')
        bad = self.peer(port)
        bad.sendall(command(client=2, sequence=2))
        self.assertEqual(bad.recv(1), b'')
        result = self.exchange(good, command(sequence=2, kind=2, price=0, quantity=0))
        self.assertEqual(result[0]['type'], 4)
        self.stop_server(server)

    def test_partial_frame_deadline_does_not_block_other_clients(self):
        server, port = self.server('--io-timeout-ms', 150)
        stalled = self.peer(port)
        stalled.sendall(command()[:8])
        live = self.peer(port)
        self.assertEqual(self.exchange(live, command(client=2))[0]['type'], 1)
        self.assertEqual(stalled.recv(1), b'')
        self.stop_server(server)

    def test_burst_preserves_fifo_and_shutdown_drains_every_market_update(self):
        server, port = self.server()
        peer = self.peer(port)
        frames = []
        for i in range(1, 2501):
            frames.append(command(sequence=2*i-1, order=i))
            frames.append(command(sequence=2*i, order=i, kind=2, price=0, quantity=0))
        peer.sendall(b''.join(frames))
        for i in range(1, 5001):
            first, done = self.response(peer), self.response(peer)
            self.assertEqual(first['type'], 1 if i % 2 else 4)
            self.assertEqual(done['type'], 6)
            self.assertEqual(done['request'], i)
        out = self.stop_server(server)
        self.assertIn('requests=5000 ', out)
        self.assertIn('updates=5000 ', out)
        self.assertIn('unroutable=0', out)

    def test_real_trading_clients_receive_both_fills_positions_and_cancel_on_stop(self):
        server, port = self.server()
        seller = self.client(port, 1, 'NEW 1 1 ASK 101 10 GTC\n', run_ms=1200)
        # stdout is flushed per report; wait for the actual exchange ACK.
        self.assertIn('report=accepted', read_line(seller.stdout))
        buyer = self.client(port, 2, 'NEW 1 1 BID 102 7 GTC\n', run_ms=350)
        bo, be = buyer.communicate(timeout=5)
        so, se = seller.communicate(timeout=5)
        self.assertEqual(buyer.returncode, 0, be.decode())
        self.assertEqual(seller.returncode, 0, se.decode())
        self.assertIn(b'report=filled', bo)
        self.assertIn(b'report=filled', so)
        self.assertIn(b'quantity=7 volume=7', bo)
        self.assertIn(b'quantity=-7 volume=7', so)
        self.assertIn(b'report=canceled', so)
        self.assertIn(b'active_orders=0', bo)
        self.assertIn(b'active_orders=0', so)
        self.stop_server(server)

    def test_late_join_client_recovers_snapshot_from_a_running_exchange(self):
        server, port = self.server()
        maker = self.peer(port)
        self.exchange(maker, command(price=99))
        self.exchange(maker, command(sequence=2, order=2, side=2, price=101))
        observer = self.client(port, 2, run_ms=250)
        out, err = observer.communicate(timeout=5)
        self.assertEqual(observer.returncode, 0, err.decode())
        self.assertRegex(out.decode(), r'summary synchronized=1 sequence=2 .*recoveries=1')
        self.stop_server(server)

    def test_random_strategy_uses_real_gateway_and_drains_live_orders(self):
        server, port = self.server()
        process = self.process('trading_client', '--tcp-port', port, '--client-id', 3,
                               '--incremental-port', self.inc, '--snapshot-port', self.snap,
                               '--strategy', 'random', '--run-ms', 300, '--interval-ms', 5)
        out, err = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 0, err.decode())
        self.assertIn(b'report=accepted', out)
        self.assertIn(b'report=canceled', out)
        self.assertIn(b'active_orders=0', out)
        self.stop_server(server)

    def test_gap_heartbeat_and_full_snapshot_recover_across_real_multicast_sockets(self):
        with socket.socket() as listener, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as publisher:
            listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(3)
            publisher.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton('127.0.0.1'))
            publisher.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
            observer = self.client(listener.getsockname()[1], 5, run_ms=500)
            peer, _ = listener.accept()
            with peer:
                def send(kind, sequence, snapshot=0, watermark=0, order=0, price=0, qty=0, count=0):
                    data = MARKET.pack(b'LBT2', 1, 3, kind, 1, 7, sequence, snapshot, watermark,
                                       1 if order else 0, order, 0, price, qty, count)
                    publisher.sendto(data, (GROUP, self.snap if snapshot else self.inc))
                send(1, 1, order=1, price=99, qty=5)
                send(1, 3, order=3, price=99, qty=5)
                send(7, 3, watermark=3)
                # UDP sockets have no cross-stream ordering: Begin can arrive
                # while the subscriber still considers its incremental current.
                # Model the publisher's periodic cycles so recovery can start
                # with the next complete cycle after observing the gap.
                for cycle in range(1, 4):
                    send(5, 0, snapshot=cycle, watermark=3, count=3)
                    for i in range(1, 4):
                        send(1, i, snapshot=cycle, watermark=3, order=i, price=99, qty=5)
                    send(6, 4, snapshot=cycle, watermark=3, count=3)
                    time.sleep(0.03)
                out, err = observer.communicate(timeout=5)
            self.assertEqual(observer.returncode, 0, err.decode())
            self.assertRegex(out.decode(), r'summary synchronized=1 sequence=3 .*recoveries=1')

    def test_invalid_configuration_and_script_fail_before_start(self):
        for args in (['--snapshot-ms', '0'], ['--tick', '0'], ['--max-price', '0'],
                     ['--tcp-port', '65536'], ['--unknown', 'x']):
            p = self.process('exchange_server', *args)
            _, err = p.communicate(timeout=5)
            self.assertEqual(p.returncode, 1, err.decode())
        path = self.temp / 'bad.orders'; path.write_text('NEW 1 2 SIDE 100 5 GTC\n')
        p = self.process('trading_client', '--orders', path)
        _, err = p.communicate(timeout=5)
        self.assertEqual(p.returncode, 1)
        self.assertIn(b'Order script line 1', err)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin-dir', type=pathlib.Path, required=True)
    args, remaining = parser.parse_known_args()
    BIN = args.bin_dir.resolve()
    unittest.main(argv=[__file__, *remaining])
