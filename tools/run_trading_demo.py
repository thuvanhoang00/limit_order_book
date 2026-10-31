#!/usr/bin/env python3
"""Run a local exchange plus maker/taker apps and retain reproducible evidence."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import selectors
import signal
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, default=ROOT / 'build/release')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    binaries = args.bin_dir.resolve()
    for name in ('exchange_server', 'exchange_replay', 'trading_app'):
        if not (binaries / name).is_file():
            raise SystemExit(f'Missing {name}; build the project first.')
    output = (args.output_dir or ROOT / 'results' / ('trading-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f'))).resolve()
    output.mkdir(parents=True, exist_ok=False)
    print(f'Artifacts: {output}', flush=True)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', 0))
        md_port = probe.getsockname()[1]
    group = '239.255.68.1'
    processes = []
    with (output / 'exchange.stderr').open('w') as server_err, (output / 'maker.stdout').open('w') as maker_out, (output / 'maker.stderr').open('w') as maker_err:
        server = subprocess.Popen([str(binaries / 'exchange_server'), '--tcp-port', '0', '--md-port', str(md_port),
                                   '--group', group, '--journal', str(output / 'exchange.journal'),
                                   '--log', str(output / 'exchange.log'), '--metrics', str(output / 'metrics.json')],
                                  stdout=subprocess.PIPE, stderr=server_err, text=True)
        processes.append(server)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(server.stdout, selectors.EVENT_READ)
                if not selector.select(10):
                    raise RuntimeError('Exchange startup deadline')
            ready = server.stdout.readline().strip()
            if not ready.startswith('READY '):
                raise RuntimeError('Exchange failed to start; inspect exchange.stderr')
            port = dict(field.split('=', 1) for field in ready.split()[1:])['tcp_port']
            base = [str(binaries / 'trading_app'), '--tcp-port', port, '--md-port', str(md_port), '--group', group]
            maker = subprocess.Popen([*base, '--client-id', '1', '--orders', str(ROOT / 'examples/trading/maker.csv')],
                                     stdout=maker_out, stderr=maker_err, text=True)
            processes.append(maker)
            deadline = time.monotonic() + 10
            while (output / 'maker.stdout').read_text().count('"report":"complete"') < 2:
                if maker.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Maker failed to place quotes; inspect maker.stderr')
                time.sleep(0.01)
            taker = subprocess.run([*base, '--client-id', '2', '--orders', str(ROOT / 'examples/trading/taker.csv')],
                                   capture_output=True, text=True, timeout=10)
            (output / 'taker.stdout').write_text(taker.stdout)
            (output / 'taker.stderr').write_text(taker.stderr)
            if taker.returncode:
                raise RuntimeError('Taker failed; inspect taker.stderr')
            if maker.wait(timeout=10):
                raise RuntimeError('Maker failed; inspect maker.stderr')
            server.send_signal(signal.SIGTERM)
            server_output, _ = server.communicate(timeout=10)
            (output / 'exchange.stdout').write_text(ready + '\n' + server_output)
            if server.returncode:
                raise RuntimeError('Exchange failed; inspect exchange.stderr')
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            server.stdout.close()
    maker_state = json.loads((output / 'maker.stdout').read_text().splitlines()[-1])
    taker_state = json.loads((output / 'taker.stdout').read_text().splitlines()[-1])
    metrics = json.loads((output / 'metrics.json').read_text())
    replay = subprocess.run([str(binaries / 'exchange_replay'), str(output / 'exchange.journal')],
                            capture_output=True, text=True, timeout=10, check=True)
    (output / 'recovery.json').write_text(replay.stdout)
    recovered = json.loads(replay.stdout)
    checks = {
        'position_conserved': maker_state['position'] + taker_state['position'] == 0,
        'cash_conserved': maker_state['cash'] + taker_state['cash'] == 0,
        'two_trades': metrics['trades'] == 2 and metrics['volume'] == 7,
        'books_synchronized': maker_state['synchronized'] and taker_state['synchronized'] and
            maker_state['market_sequence'] == taker_state['market_sequence'] == metrics['market_sequence'],
        'durable_replay_matches': recovered['trades'] == metrics['trades'] and recovered['sequence'] == metrics['market_sequence'],
    }
    summary = dict(maker=maker_state, taker=taker_state, exchange=metrics, checks=checks)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    if not all(checks.values()):
        raise SystemExit('Demo verification failed')


if __name__ == '__main__':
    main()
