#!/usr/bin/env python3
"""Measure a complete historical ITCH archive with exact latency frequencies."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ENDPOINTS = ('read_decode_apply', 'decode_apply', 'selected_update_decode_apply', 'clock_pair_overhead')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True,
                          capture_output=True, text=True).stdout


def verify(report, frequencies):
    if not report['complete_input'] or report['limited'] or not report['session_ended'] or \
            not report['invariants_valid']:
        raise ValueError('Only a complete, valid session may produce a full-archive result')
    for endpoint in ENDPOINTS:
        stats = report['latency'][endpoint]
        bins = frequencies[endpoint]
        count = sum(n for _, n in bins)
        if count != stats['count'] or not count:
            raise ValueError(f'{endpoint}: wrong/empty observation count')
        if any(ns < 0 or n <= 0 for ns, n in bins) or \
                any(bins[i][0] >= bins[i+1][0] for i in range(len(bins)-1)):
            raise ValueError(f'{endpoint}: invalid frequency ordering/counts')
        for name, permille in (('p50_ns', 500), ('p95_ns', 950), ('p99_ns', 990), ('p999_ns', 999)):
            rank = (count * permille + 999) // 1000
            accumulated = 0
            for ns, n in bins:
                accumulated += n
                if accumulated >= rank:
                    if stats[name] != ns:
                        raise ValueError(f'{endpoint}: incorrect {name}')
                    break
        mean = sum(ns * n for ns, n in bins) / count
        if abs(stats['mean_ns'] - mean) > 0.00001 or \
                stats['min_ns'] != bins[0][0] or stats['max_ns'] != bins[-1][0]:
            raise ValueError(f'{endpoint}: incorrect mean/extremes')
    if report['latency']['read_decode_apply']['count'] != report['records'] or \
            report['latency']['decode_apply']['count'] != report['records'] or \
            report['latency']['selected_update_decode_apply']['count'] != report['book_updates']:
        raise ValueError('ITCH timing counts do not match input records/selected book updates')


def render(runs):
    lines = ['# Full-archive ITCH replay latency', '',
             'Offline service times on each input record; no network or order-entry matching. '
             'All records are measured, including the first and final exchange records. Histograms and three clock reads '
             'per record add overhead; throughput is for the instrumented replay. Gzip is buffered, '
             'so read/decompression cost is concentrated in refill records.', '',
             '| Run | Path | Count | p50 (ns) | p99 (ns) | p99.9 (ns) | Max (ns) |',
             '|---|---|---:|---:|---:|---:|---:|']
    for index, run in enumerate(runs, 1):
        for endpoint in ENDPOINTS:
            s = run['latency'][endpoint]
            lines.append(f'| {index} | {endpoint} | {s["count"]:,} | {s["p50_ns"]:,} | '
                         f'{s["p99_ns"]:,} | {s["p999_ns"]:,} | {s["max_ns"]:,} |')
    lines += ['', '| Run | Seconds | Million records/s | Selected updates |',
              '|---|---:|---:|---:|']
    for index, run in enumerate(runs, 1):
        lines.append(f'| {index} | {run["elapsed_seconds"]:.3f} | '
                     f'{run["messages_per_second"] / 1e6:.3f} | {run["book_updates"]:,} |')
    lines += ['', 'Medians across per-run statistics (not pooled percentiles):', '',
              '| Path | p50 (ns) | p99 (ns) | p99.9 (ns) |', '|---|---:|---:|---:|']
    for endpoint in ENDPOINTS:
        values = [statistics.median(run['latency'][endpoint][p] for run in runs)
                  for p in ('p50_ns', 'p99_ns', 'p999_ns')]
        lines.append(f'| {endpoint} | {values[0]:,.0f} | {values[1]:,.0f} | {values[2]:,.0f} |')
    lines += ['', 'Exact nearest-rank p50/p95/p99/p99.9, means, extremes and counts were '
              'independently recalculated from the frequency JSON for every run. All runs '
              'reached exchange End of Messages and gzip EOF/CRC checks with valid final books. '
              'No clock overhead is subtracted. Startup, archive hash and warmup are before the '
              'replay timer; final invariants, histogram serialization and snapshots are after it.', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--bin-dir', type=Path, default=ROOT / 'build/release')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--symbol', default='AAPL')
    parser.add_argument('--pool-bytes', type=int, default=8 * 1024 * 1024)
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--warmup-records', type=int, default=1_000_000)
    parser.add_argument('--cpu', type=int)
    args = parser.parse_args()
    if args.repetitions < 1 or args.pool_bytes < 1 or args.warmup_records < 0:
        parser.error('repetitions/pool-bytes must be positive; warmup-records nonnegative')
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    archive = args.input.resolve()
    before = archive.stat()
    output = (args.output_dir or ROOT / 'results' / ('itch-full-' +
              datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f'))).resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = output / 'replay_market_data'
    shutil.copy2(args.bin_dir.resolve() / 'replay_market_data', binary)
    sources = ('tools/replay_market_data.cpp', 'tools/latency_histogram.hpp', 'tools/run_feed_benchmarks.py',
               'src/feed/input.cpp', 'src/feed/itch.cpp', 'src/feed/books.cpp', 'src/core/order_book.cpp')
    for name in sources:
        target = output / 'source' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    for name in ('CMakeCache.txt', 'compile_commands.json'):
        path = args.bin_dir.resolve() / name
        if path.exists(): shutil.copy2(path, output / name)
    print(f'Artifacts: {output}; hashing input archive', flush=True)
    metadata = dict(timestamp_utc=datetime.now(timezone.utc).isoformat(), invocation=sys.argv,
                    archive=str(archive), archive_bytes=before.st_size, archive_mtime_ns=before.st_mtime_ns,
                    archive_sha256=sha256(archive), binary_sha256=sha256(binary),
                    sources={name: sha256(ROOT / name) for name in sources},
                    git_head=git('rev-parse', 'HEAD').strip(), git_status=git('status', '--porcelain'),
                    platform=platform.platform(), cpu_affinity=sorted(os.sched_getaffinity(0)),
                    cpuinfo=Path('/proc/cpuinfo').read_text(),
                    cache_policy='No cache flush; full archive hash and prefix warmup precede measured runs',
                    repetitions=args.repetitions, symbol=args.symbol, pool_bytes=args.pool_bytes,
                    warmup_records=args.warmup_records)
    for name in ('scaling_governor', 'scaling_cur_freq'):
        path = Path(f'/sys/devices/system/cpu/cpu{min(os.sched_getaffinity(0))}/cpufreq/{name}')
        if path.exists(): metadata[name] = path.read_text().strip()
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (output / 'tracked.patch').write_text(git('diff', 'HEAD', '--binary'))
    base = [str(binary), '--input', str(archive), '--format', 'itch50', '--symbol', args.symbol,
            '--pool-bytes', str(args.pool_bytes)]
    commands = []

    def execute(command, label):
        commands.append(command)
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (output / (label + '.stdout')).open('w') as stdout, \
                (output / (label + '.stderr')).open('w') as stderr:
            subprocess.run(command, stdout=stdout, stderr=stderr, check=True)

    if args.warmup_records:
        print('Prefix warmup (not included in measured statistics)', flush=True)
        execute([*base, '--max-records', str(args.warmup_records)], 'warmup')
    runs = []
    for repetition in range(1, args.repetitions + 1):
        prefix = f'{repetition:02d}'
        print(f'Full archive run {repetition}/{args.repetitions}', flush=True)
        execute([*base, '--latency', '1', '--output', str(output / f'{prefix}.json'),
                 '--histogram', str(output / f'{prefix}.histogram.json'),
                 '--snapshot', str(output / f'{prefix}.snapshot.json')], prefix)
        run = json.loads((output / f'{prefix}.json').read_text())
        verify(run, json.loads((output / f'{prefix}.histogram.json').read_text()))
        current = archive.stat()
        if (current.st_size, current.st_mtime_ns) != (before.st_size, before.st_mtime_ns):
            raise ValueError('Input archive changed during measurement')
        if runs and any(run[key] != runs[0][key] for key in
                        ('records', 'messages', 'book_updates', 'trade_notices', 'books')):
            raise ValueError('Repeated replay states/counts differ')
        if runs and (output / f'{prefix}.snapshot.json').read_bytes() != (output / '01.snapshot.json').read_bytes():
            raise ValueError('Repeated FIFO book snapshots differ')
        runs.append(run)
        (output / 'summary.json').write_text(json.dumps(runs, indent=2) + '\n')
        (output / 'report.md').write_text(render(runs))
        print(f'Run {repetition}: {run["records"]:,} records, {run["messages_per_second"] / 1e6:.3f} M/s; '
              f'read/decode/apply p99={run["latency"]["read_decode_apply"]["p99_ns"]} ns; '
              'EOF/CRC/session/invariants/histogram verification passed', flush=True)


if __name__ == '__main__':
    main()
