"""Real loopback multicast processes: fan-out, book state and feed failures."""
import argparse
from contextlib import contextmanager
import pathlib
import re
import selectors
import socket
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
BIN = None
GROUP = '239.255.43.1'
INTERFACE = '127.0.0.1'
EVENT = struct.Struct('!4sBBBBQQQII')


def frame(sequence=1, kind=1, order=7, quantity=5):
    return EVENT.pack(b'LOB1', 1, kind, 1 if kind == 1 else 0, 0,
                      sequence, 55, order, 100 if kind == 1 else 0,
                      0 if kind == 2 else quantity)


class UdpCli(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='lob-udp-test-')
        self.addCleanup(temp.cleanup)
        self.temp = pathlib.Path(temp.name)

    @contextmanager
    def subscriber(self, count=60, timeout=2000, port=None, name='subscriber', log_path=None):
        if port is None:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1', 0))
                port = probe.getsockname()[1]
        log = log_path or self.temp / f'{name}.log'
        process = subprocess.Popen([str(BIN / 'udp_event_subscriber'), str(count), str(port),
                                    GROUP, INTERFACE, str(timeout), str(log)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stderr, selectors.EVENT_READ)
                self.assertTrue(selector.select(5), 'subscriber startup timeout')
            self.assertIn('Joined group=', process.stderr.readline())
            yield process, port, log
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
            process.stdout.close()
            process.stderr.close()

    def publish(self, port, csv=None):
        result = subprocess.run([str(BIN / 'udp_event_publisher'),
                                 str(csv or ROOT / 'data/sample_events.csv'), str(port),
                                 GROUP, INTERFACE, '1000'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def raw_send(self, port, packets):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                              socket.inet_aton(INTERFACE))
            sender.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
            sender.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
            for packet in packets:
                self.assertEqual(sender.sendto(packet, (GROUP, port)), len(packet))

    def test_sample_matches_existing_csv_replay_and_registry_logs(self):
        with self.subscriber() as (subscriber, port, log):
            publisher = self.publish(port)
            out, err = subscriber.communicate(timeout=5)
            self.assertEqual(subscriber.returncode, 0, err)
            self.assertIn('sent=60 ', publisher.stdout)
            self.assertEqual(out.splitlines(),
                             (ROOT / 'data/sample_events.csv').read_text().splitlines()[1:])
            self.assertIn('book_synchronized=1 sequence=60 received=60 applied=60 stale=0', err)
            replay = subprocess.run([str(BIN / 'replay_order_book'),
                                     str(ROOT / 'data/sample_events.csv'),
                                     str(self.temp / 'replay.log')],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(replay.returncode, 0, replay.stderr)
            for key in ('instruments', 'remaining_orders', 'bid_levels', 'ask_levels'):
                expected = re.search(rf'^{key}=(\d+)$', replay.stdout, re.M)
                self.assertIsNotNone(expected, replay.stdout)
                self.assertRegex(err, rf'\b{key}={expected.group(1)}\b')
            text = log.read_text()
            for event in ('UdpJoined', 'AddOrder', 'ExecuteOrder', 'CancelOrder', 'UdpFinished'):
                self.assertIn(event, text)
            self.assertNotIn('TcpAck', text)
            self.assertIn('log_format_failures=0 log_write_failed=0', err)

    def test_one_publisher_updates_two_books(self):
        with self.subscriber(name='first') as (first, port, _):
            with self.subscriber(port=port, name='second') as (second, _, _):
                self.publish(port)
                a, ea = first.communicate(timeout=5)
                b, eb = second.communicate(timeout=5)
                self.assertEqual(first.returncode, 0, ea)
                self.assertEqual(second.returncode, 0, eb)
                self.assertEqual(a, b)
                self.assertEqual(len(a.splitlines()), 60)
                self.assertIn('sequence=60 ', ea)
                self.assertIn('sequence=60 ', eb)

    def test_duplicate_and_late_old_packet_are_not_reapplied(self):
        with self.subscriber(count=3) as (subscriber, port, log):
            self.raw_send(port, [frame(), frame(), frame(2, kind=3, quantity=1),
                                 frame(), frame(3, kind=2)])
            out, err = subscriber.communicate(timeout=5)
            self.assertEqual(subscriber.returncode, 0, err)
            self.assertEqual(len(out.splitlines()), 3)
            self.assertIn('received=5 applied=3 stale=2', err)
            self.assertIn('remaining_orders=0', err)
            self.assertEqual(log.read_text().count('UdpStaleDropped'), 2)

    def test_gap_stops_before_applying_future_event(self):
        with self.subscriber(count=3) as (subscriber, port, log):
            self.raw_send(port, [frame(), frame(3, kind=2)])
            out, err = subscriber.communicate(timeout=5)
            self.assertEqual(subscriber.returncode, 1)
            self.assertIn('expected=2 received=3', err)
            self.assertIn('book_synchronized=0 sequence=1', err)
            self.assertEqual(len(out.splitlines()), 1)
            self.assertIn('UdpGapDetected', log.read_text())
            self.assertNotIn('CancelOrder', log.read_text())

    def test_late_join_requires_initial_history(self):
        with self.subscriber(count=1) as (subscriber, port, _):
            self.raw_send(port, [frame(2)])
            out, err = subscriber.communicate(timeout=5)
            self.assertEqual(subscriber.returncode, 1)
            self.assertIn('expected=1 received=2', err)
            self.assertIn('sequence=0 ', err)
            self.assertEqual(out, '')

    def test_lost_tail_is_incomplete_when_expected_count_is_not_reached(self):
        with self.subscriber(count=2, timeout=150) as (subscriber, port, log):
            self.raw_send(port, [frame()])
            out, err = subscriber.communicate(timeout=5)
            self.assertEqual(subscriber.returncode, 1)
            self.assertIn('Receive timeout', err)
            self.assertIn('book_synchronized=0 sequence=1', err)
            self.assertEqual(len(out.splitlines()), 1)
            self.assertIn('UdpTimeout', log.read_text())

    def test_malformed_datagrams_never_reach_book(self):
        for packet in (b'', b'abc', frame() + b'x', b'BAD!' + frame()[4:]):
            with self.subTest(size=len(packet)):
                with self.subscriber(count=1, name=f'bad{len(packet)}') as (subscriber, port, log):
                    self.raw_send(port, [packet])
                    out, err = subscriber.communicate(timeout=5)
                    self.assertEqual(subscriber.returncode, 1)
                    self.assertEqual(out, '')
                    self.assertIn('book_synchronized=0 sequence=0', err)
                    self.assertIn('UdpProtocolFailed', log.read_text())
                    self.assertNotIn('AddOrder', log.read_text())

    def test_apply_rejection_keeps_last_successful_sequence(self):
        with self.subscriber(count=1) as (subscriber, port, log):
            self.raw_send(port, [frame(quantity=0)])
            out, err = subscriber.communicate(timeout=5)
            self.assertEqual(subscriber.returncode, 1)
            self.assertEqual(out, '')
            self.assertIn('Book rejected event: invalid_quantity', err)
            self.assertIn('book_synchronized=0 sequence=0', err)
            self.assertIn('UdpApplyRejected', log.read_text())
            self.assertIn('result=invalid_quantity', log.read_text())

    def test_log_write_failure_returns_failure_even_after_apply(self):
        with self.subscriber(count=1, log_path=pathlib.Path('/dev/full')) as (subscriber, port, _):
            self.raw_send(port, [frame()])
            _, err = subscriber.communicate(timeout=5)
            self.assertEqual(subscriber.returncode, 1)
            self.assertIn('log_write_failed=1', err)

    def test_publisher_validates_csv_and_numeric_arguments(self):
        csv = self.temp / 'bad.csv'
        csv.write_text('broken,csv\n')
        for arguments in ([str(csv)], [str(csv), '0'], [str(csv), '9001', '127.0.0.1'],
                          [str(csv), '9001', GROUP, INTERFACE, '-1']):
            result = subprocess.run([str(BIN / 'udp_event_publisher'), *arguments],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin-dir', required=True, type=pathlib.Path)
    args, remaining = parser.parse_known_args()
    BIN = args.bin_dir.resolve()
    unittest.main(argv=[__file__, *remaining])
