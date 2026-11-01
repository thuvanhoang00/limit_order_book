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
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ENDPOINTS = ('read_decode_apply', 'decode_apply', 'selected_update_decode_apply', 'clock_pair_overhead')
TIME_FORMAT = ('{"user_seconds":%U,"system_seconds":%S,"process_elapsed_seconds":%e,'
               '"max_rss_kb":%M,"voluntary_switches":%w,"involuntary_switches":%c,'
               '"minor_faults":%R,"major_faults":%F}')


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


def main():
    from feed_report import write_report
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--bin-dir', type=Path, default=ROOT / 'build/release')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--symbol', default='AAPL')
    parser.add_argument('--pool-bytes', type=int, default=8 * 1024 * 1024)
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--warmup-records', type=int, default=1_000_000)
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--compare-instrumentation', action='store_true',
                        help='paired complete off/on runs, alternating order between pairs')
    args = parser.parse_args()
    if args.repetitions < 1 or args.pool_bytes < 1 or args.warmup_records < 0:
        parser.error('repetitions/pool-bytes must be positive; warmup-records nonnegative')
    if args.compare_instrumentation and not Path('/usr/bin/time').is_file():
        parser.error('paired process resource measurement requires GNU /usr/bin/time')
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    archive = args.input.resolve()
    before = archive.stat()
    output = (args.output_dir or ROOT / 'results' / ('itch-full-' +
              datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f'))).resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = output / 'replay_market_data'
    shutil.copy2(args.bin_dir.resolve() / 'replay_market_data', binary)
    sources = ('tools/replay_market_data.cpp', 'tools/latency_histogram.hpp', 'tools/feed_outliers.hpp',
               'tools/run_feed_benchmarks.py', 'tools/feed_report.py',
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
    metadata['compare_instrumentation'] = args.compare_instrumentation
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
    if args.compare_instrumentation:
        runs = []
        reference_snapshot = None
        for repetition in range(1, args.repetitions + 1):
            for mode in (('off', 'on') if repetition % 2 else ('on', 'off')):
                label = f'{repetition:02d}-{mode}'
                print(f'Full paired run {label}', flush=True)
                histogram = output / f'{label}.histogram.json'
                command = [*base, '--latency', '1' if mode == 'on' else '0',
                           '--output', str(output / f'{label}.json'),
                           '--snapshot', str(output / f'{label}.snapshot.json')]
                if mode == 'on': command += ['--histogram', str(histogram)]
                execute(['/usr/bin/time', '-f', TIME_FORMAT, '-o',
                         str(output / f'{label}.resources.json'), '--', *command], label)
                run = json.loads((output / f'{label}.json').read_text())
                if not run['complete_input'] or run['limited'] or not run['session_ended'] or \
                        not run['invariants_valid']:
                    raise ValueError('Paired measurement requires complete valid sessions')
                if mode == 'on': verify(run, json.loads(histogram.read_text()))
                current = archive.stat()
                if (current.st_size, current.st_mtime_ns) != (before.st_size, before.st_mtime_ns):
                    raise ValueError('Archive changed during paired measurement')
                snapshot = (output / f'{label}.snapshot.json').read_bytes()
                if runs and (any(run[key] != runs[0][key] for key in
                                ('records', 'messages', 'book_updates', 'trade_notices', 'books')) or
                             snapshot != reference_snapshot):
                    raise ValueError('Paired counts/FIFO snapshots differ')
                if reference_snapshot is None: reference_snapshot = snapshot
                run.update(mode=mode, repetition=repetition,
                           resources=json.loads((output / f'{label}.resources.json').read_text()))
                runs.append(run)
                (output / 'summary.json').write_text(json.dumps(runs, indent=2) + '\n')
                write_report(output, runs, metadata)
                print(f'{label}: {run["elapsed_seconds"]:.3f}s, '
                      f'{run["messages_per_second"]/1e6:.3f} M/s; full-state verification passed', flush=True)
        return
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
        write_report(output, runs, metadata)
        print(f'Run {repetition}: {run["records"]:,} records, {run["messages_per_second"] / 1e6:.3f} M/s; '
              f'read/decode/apply p99={run["latency"]["read_decode_apply"]["p99_ns"]} ns; '
              'EOF/CRC/session/invariants/histogram verification passed', flush=True)


if __name__ == '__main__':
    main()
