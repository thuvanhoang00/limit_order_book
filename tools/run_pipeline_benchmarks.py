#!/usr/bin/env python3
"""Measure the real client/TCP/engine/UDP pipeline with durable journaling on/off."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import selectors
import shutil
import signal
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def positive(value):
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError('must be positive')
    return result


def capture(command, **kwargs):
    return subprocess.run(command, capture_output=True, text=True, check=True, **kwargs).stdout


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(binaries, directory, iterations, warmup, journal_on):
    directory.mkdir()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', 0))
        md_port = probe.getsockname()[1]
    journal = directory / 'exchange.journal'
    group = '239.255.69.1'
    server_args = [str(binaries / 'exchange_server'), '--tcp-port', '0', '--md-port', str(md_port),
                   '--group', group, '--journal', str(journal) if journal_on else '-',
                   '--log', str(directory / 'exchange.log'),
                   '--metrics', str(directory / 'metrics.json')]
    commands = [server_args]
    with (directory / 'exchange.stderr').open('w') as stderr:
        server = subprocess.Popen(server_args, stdout=subprocess.PIPE, stderr=stderr, text=True)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(server.stdout, selectors.EVENT_READ)
                if not selector.select(10):
                    raise RuntimeError('Exchange startup deadline')
            ready = server.stdout.readline()
            if not ready.startswith('READY '):
                raise RuntimeError(f'Exchange startup failed: {directory / "exchange.stderr"}')
            port = dict(field.split('=', 1) for field in ready.split()[1:])['tcp_port']
            client_args = [str(binaries / 'trading_pipeline_benchmark'), '--tcp-port', port,
                           '--md-port', str(md_port), '--group', group,
                           '--iterations', str(iterations), '--warmup', str(warmup),
                           '--output', str(directory / 'latency.json'),
                           '--samples', str(directory / 'samples.csv')]
            commands.append(client_args)
            result = subprocess.run(client_args, capture_output=True, text=True,
                                    timeout=max(30, (iterations + warmup) * 8 * 0.05))
            (directory / 'client.stdout').write_text(result.stdout)
            (directory / 'client.stderr').write_text(result.stderr)
            result.check_returncode()
            server.send_signal(signal.SIGTERM)
            stdout, _ = server.communicate(timeout=10)
            (directory / 'exchange.stdout').write_text(ready + stdout)
            if server.returncode:
                raise RuntimeError(f'Exchange failed: {directory / "exchange.stderr"}')
        finally:
            (directory / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
            if server.poll() is None:
                server.terminate()
                try:
                    server.communicate(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.communicate()
            server.stdout.close()
    latency = json.loads((directory / 'latency.json').read_text())
    metrics = json.loads((directory / 'metrics.json').read_text())
    checks = {
        'client_verified': latency['verified'],
        'commands': metrics['commands'] == latency['total_commands'],
        'no_rejects': metrics['rejected'] == 0,
        'matching': metrics['trades'] == latency['expected_trades'] and
                    metrics['volume'] == latency['expected_volume'],
        'feed_watermark': metrics['market_sequence'] == latency['market_sequence'],
        'healthy_connections': metrics['slow_clients'] == metrics['malformed'] == 0,
    }
    if journal_on:
        replay_args = [str(binaries / 'exchange_replay'), str(journal)]
        replay = capture(replay_args, timeout=max(30, iterations))
        (directory / 'recovery.json').write_text(replay)
        commands.append(replay_args)
        (directory / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        recovered = json.loads(replay)
        checks['durable_replay'] = all(recovered[key] == metrics[key]
                                       for key in ('commands', 'trades', 'volume')) and \
            recovered['sequence'] == metrics['market_sequence'] and recovered['open_orders'] == 0
    result = dict(journal='on' if journal_on else 'off', latency=latency,
                  exchange=metrics, checks=checks, directory=directory.name)
    (directory / 'verified.json').write_text(json.dumps(result, indent=2) + '\n')
    if not all(checks.values()):
        raise RuntimeError(f'Benchmark verification failed: {directory}')
    return result


def main():
    from feed_report import write_pipeline_report
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, default=ROOT / 'build/release')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--iterations', type=positive, default=5000, help='8 commands per iteration')
    parser.add_argument('--warmup', type=int, default=100)
    parser.add_argument('--repetitions', type=positive, default=3)
    parser.add_argument('--cpus', help='optional process/worker CPU set, e.g. 2,3,4,5')
    args = parser.parse_args()
    if not 0 <= args.warmup <= 10000 or args.iterations > 100000:
        parser.error('warmup must be 0..10000; iterations must be 1..100000')
    if args.cpus:
        os.sched_setaffinity(0, {int(cpu) for cpu in args.cpus.split(',')})
    output = (args.output_dir or ROOT / 'results' / ('pipeline-' +
              datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f'))).resolve()
    binaries = args.bin_dir.resolve()
    names = ('exchange_server', 'exchange_replay', 'trading_pipeline_benchmark')
    for name in names:
        if not (binaries / name).is_file():
            parser.error(f'Missing {binaries / name}; build the project first')
    output.mkdir(parents=True, exist_ok=False)
    # Freeze binaries before any run, so rebuilding the working tree cannot mix versions.
    frozen = output / 'bin'
    frozen.mkdir()
    for name in names:
        shutil.copy2(binaries / name, frozen / name)
    for name in ('CMakeCache.txt', 'compile_commands.json'):
        if (binaries / name).exists():
            shutil.copy2(binaries / name, output / name)
    source_names = ['tools/trading_pipeline_benchmark.cpp', 'tools/run_pipeline_benchmarks.py', 'tools/feed_report.py',
                    'src/trading/client.cpp', 'src/trading/server.cpp', 'src/trading/engine.cpp']
    for name in source_names:
        target = output / 'source' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    (output / 'tracked.patch').write_text(capture(['git', 'diff', 'HEAD', '--binary'], cwd=ROOT))
    metadata = dict(timestamp_utc=datetime.now(timezone.utc).isoformat(),
                    invocation=sys.argv, git_head=capture(['git', 'rev-parse', 'HEAD'], cwd=ROOT).strip(),
                    git_status=capture(['git', 'status', '--porcelain'], cwd=ROOT),
                    platform=platform.platform(), cpu_affinity=sorted(os.sched_getaffinity(0)),
                    cpuinfo=Path('/proc/cpuinfo').read_text(),
                    binaries={name: digest(frozen / name) for name in names},
                    sources={name: digest(ROOT / name) for name in source_names},
                    iterations=args.iterations, warmup=args.warmup, repetitions=args.repetitions)
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Artifacts: {output}', flush=True)
    runs = []
    for repetition in range(1, args.repetitions + 1):
        for journal_on in ((False, True) if repetition % 2 else (True, False)):
            directory = output / f'{repetition:02d}-journal-{"on" if journal_on else "off"}'
            result = run_case(frozen, directory, args.iterations, args.warmup, journal_on)
            runs.append(result)
            (output / 'summary.json').write_text(json.dumps(runs, indent=2) + '\n')
            write_pipeline_report(output, runs)
            print(f'{directory.name}: {result["latency"]["commands_per_second"]:.1f} commands/s; '
                  f'Complete p99 {result["latency"]["complete"]["p99_ns"] / 1000:.2f} µs; '
                  'verification passed', flush=True)
    write_pipeline_report(output, runs)


if __name__ == '__main__':
    main()
