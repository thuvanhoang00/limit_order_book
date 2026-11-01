"""Native binary protocol fixtures, gzip integrity, replay state and fail-closed CLI."""
import argparse
import gzip
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

BIN = None


def itch_header(kind, locate=1):
    return kind.encode() + struct.pack('!HH', locate, 0) + (123456789).to_bytes(6, 'big')


def itch_fixture():
    messages = [itch_header('S', 0) + b'O',
                itch_header('R') + b'AAPL    ' + b'Q' + b'N' + struct.pack('!I', 100) + b'N' * 14,
                itch_header('A') + struct.pack('!QcI8sI', 42, b'B', 100, b'AAPL    ', 1234500),
                itch_header('X') + struct.pack('!QI', 42, 10),
                itch_header('U') + struct.pack('!QQII', 42, 43, 75, 1234600),
                itch_header('E') + struct.pack('!QIQ', 43, 5, 987),
                itch_header('S', 0) + b'C']
    assert len(messages[1]) == 39
    return b''.join(struct.pack('!H', len(m)) + m for m in messages)


def pitch_message(kind, payload):
    return bytes([len(payload) + 2, kind]) + payload


def pitch_fixture():
    time = pitch_message(0x20, struct.pack('<I', 36000))
    add = pitch_message(0x21, struct.pack('<IQcI6sQB', 1, 42, b'B', 100, b'AAPL  ', 1234500, 1))
    reduce = pitch_message(0x26, struct.pack('<IQH', 2, 42, 10))
    modify = pitch_message(0x27, struct.pack('<IQIQB', 3, 42, 75, 1234600, 1))
    execute = pitch_message(0x23, struct.pack('<IQIQ', 4, 42, 5, 987))
    end = pitch_message(0x2d, struct.pack('<I', 5))
    messages = time + add + reduce + modify + execute + end
    return struct.pack('<HBBI', len(messages) + 8, 6, 1, 1) + messages


class FeedReplay(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='lob-feed-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)

    def run_feed(self, data, format='itch50', compressed=False, **options):
        path = self.root / ('input.gz' if compressed else 'input.bin')
        path.write_bytes(gzip.compress(data) if compressed else data)
        snapshot = self.root / 'book.json'
        command = [str(BIN / 'replay_market_data'), '--input', str(path), '--format', format,
                   '--symbol', options.pop('symbol', 'AAPL'), '--snapshot', str(snapshot)]
        for key, value in options.items():
            command += ['--' + key.replace('_', '-'), str(value)]
        return subprocess.run(command, capture_output=True, text=True, timeout=10), snapshot

    def test_native_itch_and_pitch_raw_and_gzip_share_book_contract(self):
        for format, data in [('itch50', itch_fixture()), ('pitch', pitch_fixture())]:
            for compressed in (False, True):
                with self.subTest(format=format, compressed=compressed):
                    result, snapshot = self.run_feed(data, format, compressed)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    report = json.loads(result.stdout)
                    self.assertTrue(report['complete_input'])
                    self.assertFalse(report['limited'])
                    self.assertTrue(report['invariants_valid'])
                    self.assertEqual(report['price_scale'], 10000)
                    book = json.loads(snapshot.read_text())['books'][0]
                    self.assertEqual(book['symbol'], 'AAPL')
                    self.assertEqual(book['levels'][0]['price'], 1234600)
                    self.assertEqual(book['levels'][0]['quantity'], 70)
                    self.assertEqual(len(book['levels'][0]['orders']), 1)

    def test_truncated_records_gzip_crc_missing_end_and_unknown_messages_fail(self):
        data = itch_fixture()
        for content in (data[:-1], data[:-14], data + b'\x00', b'\x00\x01?'):
            result, _ = self.run_feed(content)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, '')
        for content in (gzip.compress(data)[:-8], gzip.compress(data)[:-1],
                        gzip.compress(data)[:-8] + b'\xff' * 8):
            path = self.root / 'broken.gz'
            path.write_bytes(content)
            result = subprocess.run([str(BIN / 'replay_market_data'), '--input', str(path)],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0, result.stdout)
        broken_pitch = bytearray(pitch_fixture())
        broken_pitch[4:8] = struct.pack('<I', 10)
        result, _ = self.run_feed(broken_pitch, 'pitch')
        self.assertNotEqual(result.returncode, 0)

    def test_prefix_replay_is_explicitly_limited_and_unsupported_venues_reject(self):
        result, _ = self.run_feed(itch_fixture(), max_records=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report['limited'])
        self.assertFalse(report['complete_input'])
        self.assertFalse(report['session_ended'])
        self.assertEqual(report['records'], 3)
        result, _ = self.run_feed(itch_fixture(), venue='hose')
        self.assertNotEqual(result.returncode, 0)

    def test_exact_latency_histograms_match_recomputed_statistics(self):
        for format, data, records, selected in [('itch50', itch_fixture(), 7, 4),
                                                ('pitch', pitch_fixture(), 1, 1)]:
            histogram = self.root / 'latencies.json'
            result, _ = self.run_feed(data, format, True, latency=1, histogram=histogram)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            frequencies = json.loads(histogram.read_text())
            self.assertEqual(report['latency']['unit'], 'input_record')
            counts = {'read_decode_apply': records, 'decode_apply': records,
                      'selected_update_decode_apply': selected, 'clock_pair_overhead': 10000}
            for endpoint, count in counts.items():
                distribution = report['latency'][endpoint]
                samples = frequencies[endpoint]
                self.assertEqual(sum(n for _, n in samples), count)
                self.assertEqual(distribution['count'], count)
                self.assertEqual(samples, sorted(samples))
                self.assertEqual(samples[0][0], distribution['min_ns'])
                self.assertEqual(samples[-1][0], distribution['max_ns'])
                self.assertAlmostEqual(distribution['mean_ns'],
                                       sum(ns * n for ns, n in samples) / count, places=4)
                for name, permille in [('p50_ns', 500), ('p95_ns', 950),
                                        ('p99_ns', 990), ('p999_ns', 999)]:
                    rank = (count * permille + 999) // 1000
                    accumulated = 0
                    for value, n in samples:
                        accumulated += n
                        if accumulated >= rank:
                            self.assertEqual(value, distribution[name])
                            break
            self.assertTrue(report['complete_input'])

    def test_histogram_options_cannot_overwrite_input(self):
        path = self.root / 'input.bin'
        result, _ = self.run_feed(itch_fixture(), latency=1, histogram=path)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(path.read_bytes(), itch_fixture())
        result, _ = self.run_feed(itch_fixture(), histogram=self.root / 'hist.json')
        self.assertNotEqual(result.returncode, 0)
        result, _ = self.run_feed(itch_fixture(), symbol='MISSING')
        self.assertNotEqual(result.returncode, 0)

    def test_full_archive_runner_verifies_and_retains_histograms(self):
        source = self.root / 'archive.gz'
        source.write_bytes(gzip.compress(itch_fixture()))
        output = self.root / 'measurement'
        script = Path(__file__).resolve().parents[2] / 'tools/run_feed_benchmarks.py'
        command = [sys.executable, str(script), '--input', str(source), '--bin-dir', str(BIN),
                   '--output-dir', str(output), '--repetitions', '2', '--warmup-records', '0']
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        runs = json.loads((output / 'summary.json').read_text())
        self.assertEqual(len(runs), 2)
        self.assertTrue(all(run['complete_input'] and run['records'] == 7 for run in runs))
        self.assertEqual((output / '01.snapshot.json').read_bytes(),
                         (output / '02.snapshot.json').read_bytes())
        previous = (output / 'summary.json').read_bytes()
        again = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(again.returncode, 0)
        self.assertEqual((output / 'summary.json').read_bytes(), previous)

    def test_separate_outlier_diagnostic_tracks_only_selected_order_updates(self):
        trace = self.root / 'outliers.json'
        result, _ = self.run_feed(itch_fixture(), outlier_trace=trace,
                                  outlier_threshold_ns=1, outlier_capacity=2)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['instrumentation'], 'outlier_trace')
        self.assertNotIn('latency', report)
        data = json.loads(trace.read_text())
        self.assertEqual(data['observations'], 4)
        self.assertEqual(data['slow_records'], 4)
        self.assertEqual(data['retained'], 2)
        self.assertEqual([r['wall_ns'] for r in data['records']],
                         sorted((r['wall_ns'] for r in data['records']), reverse=True))
        for row in data['records']:
            self.assertIn(row['record'], (3, 4, 5, 6))
            for field in ('thread_cpu_ns', 'voluntary_switches', 'involuntary_switches',
                          'minor_faults', 'major_faults', 'estimated_off_cpu_ns'):
                self.assertGreaterEqual(row[field], 0)
        result, _ = self.run_feed(itch_fixture(), latency=1, outlier_trace=trace)
        self.assertNotEqual(result.returncode, 0)

    def test_runner_paired_modes_alternate_and_preserve_identical_snapshots(self):
        source = self.root / 'archive.gz'
        source.write_bytes(gzip.compress(itch_fixture()))
        output = self.root / 'paired'
        script = Path(__file__).resolve().parents[2] / 'tools/run_feed_benchmarks.py'
        result = subprocess.run([sys.executable, str(script), '--input', str(source),
            '--bin-dir', str(BIN), '--output-dir', str(output), '--repetitions', '2',
            '--warmup-records', '0', '--compare-instrumentation'],
            capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        runs = json.loads((output / 'summary.json').read_text())
        self.assertEqual([r['mode'] for r in runs], ['off', 'on', 'on', 'off'])
        for r in runs:
            self.assertEqual(r['records'], 7)
            self.assertTrue(r['complete_input'])
            self.assertEqual('latency' in r, r['mode'] == 'on')
            self.assertGreater(r['resources']['max_rss_kb'], 0)
        snapshots = [p.read_bytes() for p in output.glob('*.snapshot.json')]
        self.assertEqual(len(snapshots), 4)
        self.assertTrue(all(s == snapshots[0] for s in snapshots))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin-dir', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    BIN = args.bin_dir.resolve()
    unittest.main(argv=[__file__, *remaining])
