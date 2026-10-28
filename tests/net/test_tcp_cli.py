"""Two-process ingress, sequencing, protocol failures and asynchronous logging."""
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
EVENT = struct.Struct('!4sBBBBQQQII')
ACK = struct.Struct('!4sBBBBQ')


def frame(sequence=1, quantity=5):
    return EVENT.pack(b'LOB1', 1, 1, 1, 0, sequence, 55, 7, 100, quantity)


def receive_exact(peer, size):
    result = b''
    while len(result) < size:
        chunk = peer.recv(size - len(result))
        if not chunk:
            raise AssertionError('Peer closed inside ACK')
        result += chunk
    return result


class TcpCli(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='lob-tcp-test-')
        self.addCleanup(temporary.cleanup)
        self.temp = pathlib.Path(temporary.name)

    def csv(self, content):
        path = self.temp / 'events.csv'
        path.write_text(content)
        return path

    @contextmanager
    def server(self, log_path=None):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        log = log_path or self.temp / 'server.log'
        process = subprocess.Popen([str(BIN / 'tcp_event_server'), str(port), str(log)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stderr, selectors.EVENT_READ)
                self.assertTrue(selector.select(5), 'server readiness timeout')
            self.assertIn('Listening', process.stderr.readline())
            yield process, port, log
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
            process.stdout.close()
            process.stderr.close()

    def exchange(self, csv, server_log=None, client_log=None):
        client_log = client_log or self.temp / 'client.log'
        with self.server(server_log) as (server, port, log):
            client = subprocess.run([str(BIN / 'tcp_event_client'), str(csv), str(port),
                                     str(client_log)], capture_output=True, text=True, timeout=10)
            out, err = server.communicate(timeout=5)
            return client, server.returncode, out, err, log.read_text() if log.is_file() else ''

    def test_sample_matches_csv_replay_and_logs_session(self):
        sample = ROOT / 'data/sample_events.csv'
        client, status, out, err, log = self.exchange(sample)
        self.assertEqual(client.returncode, 0, client.stderr)
        self.assertEqual(status, 0, err)
        self.assertEqual(out.splitlines(), sample.read_text().splitlines()[1:])
        self.assertEqual(client.stdout.splitlines(),
                         [f'apply_ack sequence={i} result=ok' for i in range(1, 61)])
        replay = subprocess.run([str(BIN / 'replay_order_book'), str(sample),
                                 str(self.temp / 'replay.log')],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(replay.returncode, 0, replay.stderr)
        for key in ('instruments', 'remaining_orders', 'bid_levels', 'ask_levels'):
            expected = re.search(rf'^{key}=(\d+)$', replay.stdout, re.M)
            self.assertIsNotNone(expected, replay.stdout)
            self.assertRegex(err, rf'\b{key}={expected.group(1)}\b')
        self.assertIn('sequence=60 ', err)
        for event in ('TcpListening', 'TcpConnected', 'AddOrder', 'CancelOrder',
                      'ExecuteOrder', 'TcpAckSent', 'TcpDisconnected', 'TcpFinished'):
            self.assertIn(event, log)
        client_log = (self.temp / 'client.log').read_text()
        for event in ('TcpConnected', 'TcpAckReceived', 'TcpDisconnected', 'TcpFinished'):
            self.assertIn(event, client_log)
        self.assertIn('TcpFinished sequence=60', log)
        self.assertIn('TcpFinished sequence=60', client_log)
        self.assertIn('log_format_failures=0 log_write_failed=0', err)
        self.assertIn('log_format_failures=0 log_write_failed=0', client.stderr)

    def test_duplicate_returns_rejection_without_advancing_sequence(self):
        csv = self.csv('1,55,ADD,7,BID,100,5\n2,55,ADD,7,BID,100,5\n')
        client, status, _, err, log = self.exchange(csv)
        self.assertEqual(client.returncode, 1)
        self.assertIn('apply_ack sequence=2 result=duplicate_order', client.stdout)
        self.assertEqual(status, 0, err)
        self.assertIn('remaining_orders=1', err)
        self.assertIn('sequence=1 ', err)
        self.assertIn('result=duplicate_order', log)
        self.assertIn('result=duplicate_order', (self.temp / 'client.log').read_text())

    def test_invalid_sequence_closes_before_apply(self):
        csv = self.csv('2,55,ADD,7,BID,100,5\n')
        client, status, out, err, log = self.exchange(csv)
        self.assertEqual(client.returncode, 1)
        self.assertEqual(status, 1)
        self.assertEqual(out, '')
        self.assertIn('Invalid event sequence', err)
        self.assertNotIn('apply_ack', client.stdout)
        self.assertIn('InvalidSequence sequence=2 instrument_id=55', log)
        self.assertNotIn('AddOrder', log)

    def test_rejected_event_allows_corrected_retry_on_same_connection(self):
        with self.server() as (server, port, log):
            with socket.create_connection(('127.0.0.1', port), timeout=5) as peer:
                for event, sequence, expected in ((frame(quantity=0), 1, 4),
                                                   (frame(), 1, 0),
                                                   (EVENT.pack(b'LOB1', 1, 3, 0, 0,
                                                               2, 55, 7, 0, 5), 2, 0)):
                    peer.sendall(event)
                    self.assertEqual(ACK.unpack(receive_exact(peer, ACK.size)),
                                     (b'LOB1', 1, 129, expected, 0, sequence))
            _, err = server.communicate(timeout=5)
            self.assertEqual(server.returncode, 0, err)
            self.assertIn('sequence=2 ', err)
            self.assertIn('remaining_orders=0', err)
            text = log.read_text()
            self.assertIn('result=invalid_quantity', text)
            self.assertIn('TcpFinished sequence=2', text)

    def test_malformed_frame_is_logged_and_not_applied(self):
        with self.server() as (server, port, log):
            with socket.create_connection(('127.0.0.1', port), timeout=5) as peer:
                peer.sendall(b'BAD!' + frame()[4:])
                self.assertEqual(peer.recv(1), b'')
            out, err = server.communicate(timeout=5)
            self.assertEqual(server.returncode, 1)
            self.assertEqual(out, '')
            self.assertIn('Malformed event frame', err)
            self.assertIn('TcpProtocolFailed', log.read_text())
            self.assertNotIn('AddOrder', log.read_text())

    def test_truncated_frame_is_logged_as_transport_failure(self):
        with self.server() as (server, port, log):
            with socket.create_connection(('127.0.0.1', port), timeout=5) as peer:
                peer.sendall(frame()[:9])
                peer.shutdown(socket.SHUT_WR)
                self.assertEqual(peer.recv(1), b'')
            out, err = server.communicate(timeout=5)
            self.assertEqual(server.returncode, 1)
            self.assertEqual(out, '')
            self.assertIn('inside a frame', err)
            self.assertIn('TcpTransportFailed', log.read_text())

    def fake_server(self, response):
        csv = self.csv('1,55,ADD,7,BID,100,5\n')
        log = self.temp / 'client.log'
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(5)
            port = listener.getsockname()[1]
            client = subprocess.Popen([str(BIN / 'tcp_event_client'), str(csv), str(port),
                                       str(log)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      text=True)
            try:
                peer, _ = listener.accept()
                with peer:
                    peer.settimeout(5)
                    receive_exact(peer, EVENT.size)
                    if response:
                        peer.sendall(response)
                out, err = client.communicate(timeout=5)
                return client.returncode, out, err, log.read_text() if log.is_file() else ''
            finally:
                if client.poll() is None:
                    client.kill()
                    client.communicate()
                client.stdout.close()
                client.stderr.close()

    def test_client_logs_mismatched_ack(self):
        status, out, err, log = self.fake_server(ACK.pack(b'LOB1', 1, 129, 0, 0, 2))
        self.assertEqual(status, 1)
        self.assertEqual(out, '')
        self.assertIn('Invalid or mismatched ACK', err)
        self.assertIn('TcpProtocolFailed sequence=1 instrument_id=55', log)

    def test_client_logs_disconnect_before_ack(self):
        status, out, err, log = self.fake_server(None)
        self.assertEqual(status, 1)
        self.assertEqual(out, '')
        self.assertIn('outcome unknown', err)
        self.assertIn('TcpTransportFailed sequence=1 instrument_id=55', log)

    def test_client_logs_parse_failure(self):
        client, status, out, err, _ = self.exchange(self.csv('not,a,valid,csv\n'))
        self.assertEqual(client.returncode, 1)
        self.assertEqual(status, 0, err)
        self.assertEqual(out, '')
        log = (self.temp / 'client.log').read_text()
        self.assertIn('ParseFailed', log)
        self.assertIn('result=parse_error line=1', log)

    def test_client_logs_refused_connection_with_error_code(self):
        # Keep the port reserved without listening, avoiding another listener taking it.
        with socket.socket() as reserved:
            reserved.bind(('127.0.0.1', 0))
            port = reserved.getsockname()[1]
            log = self.temp / 'client.log'
            result = subprocess.run([str(BIN / 'tcp_event_client'),
                                     str(self.csv('')), str(port), str(log)],
                                    capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertIn('TcpTransportFailed', log.read_text())
        self.assertRegex(log.read_text(), r'error_code=[1-9][0-9]*')

    def test_server_reports_log_write_failure(self):
        client, status, _, err, _ = self.exchange(self.csv('1,55,ADD,7,BID,100,5\n'),
                                                  server_log=pathlib.Path('/dev/full'))
        self.assertEqual(client.returncode, 0, client.stderr)
        self.assertEqual(status, 1)
        self.assertIn('log_write_failed=1', err)

    def test_client_reports_log_write_failure(self):
        client, status, _, err, _ = self.exchange(self.csv('1,55,ADD,7,BID,100,5\n'),
                                                  client_log=pathlib.Path('/dev/full'))
        self.assertEqual(client.returncode, 1)
        self.assertEqual(status, 0, err)
        self.assertIn('log_write_failed=1', client.stderr)

    def test_invalid_log_path_fails_before_listen(self):
        result = subprocess.run([str(BIN / 'tcp_event_server'), '9000',
                                 str(self.temp / 'missing' / 'server.log')],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertIn('Failed to open log file', result.stderr)
        self.assertNotIn('Listening', result.stderr)

    def test_invalid_port_is_rejected(self):
        for port in ('0', '65536', '-1', 'abc', '9000x'):
            result = subprocess.run([str(BIN / 'tcp_event_server'), port],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertIn('Port must be in 1..65535', result.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin-dir', required=True, type=pathlib.Path)
    args, remaining = parser.parse_known_args()
    BIN = args.bin_dir.resolve()
    unittest.main(argv=[__file__, *remaining])
