"""Real TCP/multicast processes; includes durability and private fill routing."""
import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import re
import selectors
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from concurrent.futures import ThreadPoolExecutor

BIN = None
ROOT = Path(__file__).resolve().parents[2]
SIZE = 96
GROUP = '239.255.67.1'

def pack(client=1, request=1, order=1, side=0, price=100, quantity=5,
         action=1, instrument=1, order_type=1, tif=1, post_only=False):
    b = bytearray(SIZE)
    b[:14] = bytes([76, 84, 82, 49, 1, 1, action, side, order_type, tif, 1, 0, int(post_only), 1])
    struct.pack_into('!IIQQQQIIQ', b, 32, client, price, request, order, 0, instrument, quantity, 0, 0)
    return b

def unpack(b):
    if len(b) != SIZE or b[:5] != b'LTR1\x01':
        raise AssertionError(f'Bad frame: {b!r}')
    return dict(kind=b[5], report=b[10], reject=b[11], update=b[13],
                epoch=struct.unpack_from('!Q', b, 16)[0], sequence=struct.unpack_from('!Q', b, 24)[0],
                client=struct.unpack_from('!I', b, 32)[0], price=struct.unpack_from('!I', b, 36)[0],
                request=struct.unpack_from('!Q', b, 40)[0], order=struct.unpack_from('!Q', b, 48)[0],
                exchange_id=struct.unpack_from('!Q', b, 56)[0], instrument=struct.unpack_from('!Q', b, 64)[0],
                quantity=struct.unpack_from('!I', b, 72)[0], remaining=struct.unpack_from('!I', b, 76)[0],
                trade=struct.unpack_from('!Q', b, 80)[0])

def recv(sock):
    b = bytearray()
    while len(b) < SIZE:
        data = sock.recv(SIZE - len(b))
        if not data:
            raise AssertionError('Unexpected TCP EOF')
        b.extend(data)
    return unpack(b)

def command(sock, data):
    sock.sendall(data)
    reports = []
    while True:
        m = recv(sock)
        reports.append(m)
        if m['report'] == 6:
            return reports


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


class TradingSystem(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='lob-system-test-')
        self.addCleanup(temp.cleanup)
        self.temp = Path(temp.name)
        self.md_port = free_udp_port()

    @contextmanager
    def server(self, journal='-', rules=None):
        args = [str(BIN / 'exchange_server'), '--tcp-port', '0', '--md-port', str(self.md_port),
                '--group', GROUP, '--journal', str(journal), '--log', str(self.temp / 'exchange.log'),
                '--metrics', str(self.temp / 'metrics.json')]
        if rules:
            args += ['--rules', str(rules)]
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                self.assertTrue(selector.select(10), 'exchange startup deadline')
            line = process.stdout.readline()
            self.assertIn('READY ', line, line + process.stderr.read() if process.poll() is not None else line)
            port = int(re.search(r'tcp_port=(\d+)', line).group(1))
            yield port, process
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            out, err = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, out + err)
            self.assertNotIn('AddressSanitizer', err)
            self.assertNotIn('runtime error:', err)

    @contextmanager
    def connection(self, port):
        with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            yield sock

    def snapshot(self, port):
        with self.connection(port) as sock:
            sock.sendall(pack(client=0, request=0, action=5))
            rows = []
            while True:
                m = recv(sock)
                rows.append(m)
                if m['kind'] == 7:
                    self.assertEqual(rows[0]['kind'], 5)
                    self.assertEqual(len(rows) - 2, rows[0]['quantity'])
                    self.assertTrue(all(row['sequence'] == rows[0]['sequence'] for row in rows))
                    return rows

    def test_fragmentation_coalescing_matching_and_unsolicited_maker_fill(self):
        with self.server() as (port, _):
            with self.connection(port) as maker, self.connection(port) as taker:
                data = pack(client=1, side=1, price=101, quantity=7)
                maker.sendall(data[:3]); maker.sendall(data[3:40]); maker.sendall(data[40:])
                self.assertEqual(recv(maker)['report'], 1)
                self.assertEqual(recv(maker)['report'], 6)
                taker.sendall(pack(client=2, price=101, quantity=3) +
                              pack(client=2, request=2, order=2, side=1, price=110, quantity=1))
                responses = [recv(taker) for _ in range(5)]
                self.assertEqual([r['report'] for r in responses], [1, 3, 6, 1, 6])
                passive = recv(maker)
                self.assertEqual(passive['report'], 3)
                self.assertEqual(passive['trade'], responses[1]['trade'])
                self.assertEqual(passive['quantity'], 3)
                rows = self.snapshot(port)
                self.assertEqual([(r['price'], r['quantity']) for r in rows[1:-1]], [(101, 4), (110, 1)])

    def test_duplicate_request_is_idempotent_and_session_identity_is_bound(self):
        with self.server() as (port, _):
            with self.connection(port) as one, self.connection(port) as two:
                data = pack()
                first = command(one, data)
                again = command(one, data)
                self.assertEqual(first, again)
                self.assertEqual(len(self.snapshot(port)) - 2, 1)
                two.sendall(pack(request=0, action=6))
                self.assertEqual(recv(two)['reject'], 16)
                one.sendall(pack(client=2, request=2, order=2))
                self.assertEqual(one.recv(1), b'')

    def test_slow_partial_client_does_not_block_healthy_clients(self):
        with self.server() as (port, _):
            with self.connection(port) as partial, self.connection(port) as healthy:
                partial.sendall(pack()[:5])
                self.assertEqual(command(healthy, pack(client=2))[0]['report'], 1)
                self.assertEqual(len(self.snapshot(port)) - 2, 1)

    def test_multicast_fanout_and_heartbeat_watermark(self):
        with self.server() as (port, _):
            receivers = []
            try:
                for _ in range(2):
                    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    s.bind((GROUP, self.md_port))
                    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                                 socket.inet_aton(GROUP) + socket.inet_aton('127.0.0.1'))
                    s.settimeout(3)
                    receivers.append(s)
                with self.connection(port) as gateway:
                    command(gateway, pack())
                    deltas = []
                    for s in receivers:
                        while True:
                            m = unpack(s.recv(SIZE + 1))
                            if m['kind'] == 3:
                                deltas.append(m)
                                break
                    self.assertEqual(deltas[0], deltas[1])
                    self.assertEqual(deltas[0]['sequence'], 1)
                    while True:
                        hb = unpack(receivers[0].recv(SIZE + 1))
                        if hb['kind'] == 4 and hb['sequence'] == 1:
                            break
            finally:
                for s in receivers:
                    s.close()

    def test_restart_replays_book_ids_sequences_and_last_request_cache(self):
        journal = self.temp / 'durable.journal'
        with self.server(journal) as (port, _):
            with self.connection(port) as maker, self.connection(port) as taker:
                command(maker, pack(client=1, side=1, price=100, quantity=7))
                data = pack(client=2, price=100, quantity=3)
                first = command(taker, data)
                before = self.snapshot(port)
        with self.server(journal) as (port, _):
            after = self.snapshot(port)
            self.assertNotEqual(after[0]['epoch'], before[0]['epoch'])
            self.assertEqual(after[0]['sequence'], before[0]['sequence'])
            self.assertEqual(after[1]['exchange_id'], before[1]['exchange_id'])
            self.assertEqual(after[1]['quantity'], 4)
            with self.connection(port) as gateway:
                repeated = command(gateway, data)
                self.assertEqual(repeated[1]['trade'], first[1]['trade'])
                self.assertEqual(self.snapshot(port)[1]['quantity'], 4)
        replay = subprocess.run([str(BIN / 'exchange_replay'), str(journal)], capture_output=True, text=True, timeout=5)
        self.assertEqual(replay.returncode, 0, replay.stderr)
        summary = json.loads(replay.stdout)
        self.assertEqual(summary['trades'], 1)
        self.assertEqual(summary['open_orders'], 1)

    def test_trading_apps_receive_fills_track_positions_and_recover_after_bad_udp(self):
        maker_script = self.temp / 'maker.csv'
        maker_script.write_text('NEW,1,1,SELL,LIMIT,GTC,101,10,1\nWAIT,1200\n')
        taker_script = self.temp / 'taker.csv'
        taker_script.write_text('NEW,1,1,BUY,LIMIT,IOC,101,4,0\n')
        with self.server() as (port, _):
            base = [str(BIN / 'trading_app'), '--tcp-port', str(port), '--md-port', str(self.md_port), '--group', GROUP, '--cancel-on-exit', '0']
            maker = subprocess.Popen([*base, '--client-id', '10', '--orders', str(maker_script)],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 5
                while len(self.snapshot(port)) < 3:
                    self.assertIsNone(maker.poll(), 'maker exited before accepting order')
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.01)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                    sender.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton('127.0.0.1'))
                    sender.sendto(b'bad', (GROUP, self.md_port))
                taker = subprocess.run([*base, '--client-id', '11', '--orders', str(taker_script)],
                                       capture_output=True, text=True, timeout=10)
                self.assertEqual(taker.returncode, 0, taker.stderr)
                out, err = maker.communicate(timeout=10)
                self.assertEqual(maker.returncode, 0, err)
                maker_summary = json.loads(out.splitlines()[-1])
                taker_summary = json.loads(taker.stdout.splitlines()[-1])
                self.assertEqual(maker_summary['position'], -4)
                self.assertEqual(taker_summary['position'], 4)
                self.assertEqual(maker_summary['cash'] + taker_summary['cash'], 0)
                self.assertGreaterEqual(maker_summary['recoveries'], 3)
                self.assertTrue(maker_summary['synchronized'])
                self.assertEqual(self.snapshot(port)[1]['quantity'], 6)
            finally:
                if maker.poll() is None:
                    maker.kill()
                maker.communicate()

    def test_concurrent_clients_receive_every_order_completion(self):
        with self.server() as (port, _):
            def trade(client):
                with self.connection(port) as gateway:
                    wire = b''.join(pack(client=client, request=i, order=i, price=90, quantity=1)
                                    for i in range(1, 61))
                    gateway.sendall(wire)
                    for i in range(1, 61):
                        accepted = recv(gateway)
                        completed = recv(gateway)
                        self.assertEqual((accepted['report'], completed['report']), (1, 6))
                        self.assertEqual(completed['request'], i)
            with ThreadPoolExecutor(max_workers=6) as pool:
                list(pool.map(trade, range(1, 7)))
            self.assertEqual(len(self.snapshot(port)) - 2, 360)

    def test_configured_tick_lot_rules_validate_actual_order_entry(self):
        with self.server(rules=ROOT / 'configs/instruments.csv') as (port, _):
            with self.connection(port) as client:
                bad_tick = command(client, pack(instrument=2, price=101, quantity=10))
                self.assertEqual(bad_tick[0]['reject'], 3)
                bad_lot = command(client, pack(request=2, instrument=2, price=100, quantity=11))
                self.assertEqual(bad_lot[0]['reject'], 4)
                valid = command(client, pack(request=3, instrument=2, price=100, quantity=10))
                self.assertEqual(valid[0]['report'], 1)
                self.assertEqual(self.snapshot(port)[1]['instrument'], 2)

    def test_actual_maker_and_taker_strategies_run_to_completion(self):
        with self.server() as (port, _):
            base = [str(BIN / 'trading_app'), '--tcp-port', str(port), '--md-port', str(self.md_port), '--group', GROUP]
            maker = subprocess.Popen([*base, '--client-id', '100', '--strategy', 'maker', '--duration-ms', '1500'],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 5
                while len(self.snapshot(port)) < 4:
                    self.assertIsNone(maker.poll())
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.01)
                taker = subprocess.run([*base, '--client-id', '101', '--strategy', 'taker',
                                        '--fair-price', '105', '--duration-ms', '400'],
                                       capture_output=True, text=True, timeout=10)
                self.assertEqual(taker.returncode, 0, taker.stderr)
                out, err = maker.communicate(timeout=10)
                self.assertEqual(maker.returncode, 0, err)
                a = json.loads(out.splitlines()[-1])
                b = json.loads(taker.stdout.splitlines()[-1])
                self.assertGreater(b['volume'], 0)
                self.assertEqual(a['position'] + b['position'], 0)
                self.assertEqual(a['cash'] + b['cash'], 0)
                self.assertEqual(a['open_orders'], 0)
                self.assertEqual(b['open_orders'], 0)
            finally:
                if maker.poll() is None:
                    maker.kill()
                maker.communicate()

    def test_snapshot_streams_both_full_default_books(self):
        with self.server() as (port, _):
            for owner in range(1, 9):
                instrument = 1 if owner <= 4 else 2
                with self.connection(port) as client:
                    client.sendall(b''.join(pack(client=owner, request=i, order=i, price=90,
                                                quantity=1, instrument=instrument) for i in range(1, 129)))
                    for _ in range(128):
                        self.assertEqual(recv(client)['report'], 1)
                        self.assertEqual(recv(client)['report'], 6)
            self.assertEqual(len(self.snapshot(port)) - 2, 1024)

    def test_half_close_preserves_reports_for_admitted_command(self):
        with self.server() as (port, _):
            with self.connection(port) as client:
                client.sendall(pack())
                client.shutdown(socket.SHUT_WR)
                self.assertEqual(recv(client)['report'], 1)
                self.assertEqual(recv(client)['report'], 6)
                self.assertEqual(client.recv(1), b'')

    def test_five_thousand_command_burst_preserves_fifo_and_drains(self):
        with self.server() as (port, _):
            with self.connection(port) as client:
                wire = []
                for i in range(1, 2501):
                    wire.append(pack(request=2*i-1, order=i, price=90, quantity=1))
                    wire.append(pack(request=2*i, order=i, action=2, price=0, quantity=0))
                client.sendall(b''.join(wire))
                for i in range(1, 5001):
                    first, complete = recv(client), recv(client)
                    self.assertEqual(first['report'], 1 if i % 2 else 4)
                    self.assertEqual(complete['report'], 6)
                    self.assertEqual(complete['request'], i)
                self.assertEqual(len(self.snapshot(port)), 2)

    def test_sigterm_cancels_maker_orders_and_joins_workers(self):
        with self.server() as (port, _):
            maker = subprocess.Popen([str(BIN / 'trading_app'), '--tcp-port', str(port),
                '--md-port', str(self.md_port), '--group', GROUP, '--client-id', '70',
                '--strategy', 'maker', '--duration-ms', '10000'],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 5
                while len(self.snapshot(port)) < 4:
                    self.assertIsNone(maker.poll())
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.01)
                maker.send_signal(signal.SIGTERM)
                out, err = maker.communicate(timeout=5)
                self.assertEqual(maker.returncode, 0, err)
                self.assertEqual(json.loads(out.splitlines()[-1])['open_orders'], 0)
                self.assertEqual(len(self.snapshot(port)), 2)
            finally:
                if maker.poll() is None: maker.kill()
                maker.communicate()

    def test_microprice_maker_quotes_from_weighted_market(self):
        with self.server() as (port, _):
            with self.connection(port) as seed:
                command(seed, pack(side=0, price=90, quantity=10))
                command(seed, pack(request=2, order=2, side=1, price=110, quantity=30))
                maker = subprocess.Popen([str(BIN / 'trading_app'), '--tcp-port', str(port),
                    '--md-port', str(self.md_port), '--group', GROUP, '--client-id', '71',
                    '--strategy', 'maker', '--pricing', 'microprice', '--clip', '1', '--duration-ms', '500'],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    deadline = time.monotonic() + 5
                    while True:
                        prices = {row['price'] for row in self.snapshot(port)[1:-1]}
                        if {93, 97}.issubset(prices): break
                        self.assertIsNone(maker.poll())
                        self.assertLess(time.monotonic(), deadline)
                        time.sleep(0.01)
                    out, err = maker.communicate(timeout=5)
                    self.assertEqual(maker.returncode, 0, err)
                    self.assertEqual(json.loads(out.splitlines()[-1])['open_orders'], 0)
                finally:
                    if maker.poll() is None: maker.kill()
                    maker.communicate()

    def test_trade_ratio_taker_uses_pre_execute_depth_once(self):
        with self.server() as (port, _):
            with self.connection(port) as seed:
                command(seed, pack(side=0, price=90, quantity=20))
                command(seed, pack(request=2, order=2, side=1, price=100, quantity=20))
                taker = subprocess.Popen([str(BIN / 'trading_app'), '--tcp-port', str(port),
                    '--md-port', str(self.md_port), '--group', GROUP, '--client-id', '72',
                    '--strategy', 'taker', '--signal', 'trade-ratio', '--trade-ratio', '0.2',
                    '--clip', '2', '--duration-ms', '500'], stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, text=True)
                try:
                    self.assertIn('CLIENT_READY', taker.stdout.readline())
                    with self.connection(port) as aggressor:
                        command(aggressor, pack(client=3, price=100, quantity=5))
                    out, err = taker.communicate(timeout=5)
                    self.assertEqual(taker.returncode, 0, err)
                    state = json.loads(out.splitlines()[-1])
                    self.assertEqual(state['volume'], 2)
                    self.assertEqual(state['position'], 2)
                finally:
                    if taker.poll() is None: taker.kill()
                    taker.communicate()

    def test_shutdown_drains_accepted_requests_and_persists_them(self):
        journal = self.temp / 'drain.journal'
        with self.server(journal) as (port, process):
            with self.connection(port) as client:
                data = b''.join(pack(client=1, request=i, order=i, price=90, quantity=1) for i in range(1, 51))
                client.sendall(data)
                for i in range(1, 51):
                    self.assertEqual(recv(client)['report'], 1)
                    self.assertEqual(recv(client)['report'], 6)
                process.send_signal(signal.SIGTERM)
        with self.server(journal) as (port, _):
            self.assertEqual(len(self.snapshot(port)) - 2, 50)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin-dir', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    BIN = args.bin_dir.resolve()
    unittest.main(argv=[__file__, *remaining])
