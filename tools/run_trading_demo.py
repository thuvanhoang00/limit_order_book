#!/usr/bin/env python3
"""Run the complete exchange, two strategies, and scripted liquidity/flow."""
import argparse
import datetime
import pathlib
import re
import signal
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(('127.0.0.1', 0))
        return peer.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=pathlib.Path, default=ROOT / 'build/debug')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    binary = args.bin_dir.resolve()
    output = args.output or ROOT / 'results' / datetime.datetime.now().strftime('trading-demo-%Y%m%d-%H%M%S-%f')
    output.mkdir(parents=True, exist_ok=False)
    processes, files = [], []
    inc, snap = free_port(), free_port()
    while inc == snap:
        snap = free_port()
    multicast = ['--incremental-port', str(inc), '--snapshot-port', str(snap)]

    def spawn(name, executable, arguments, ready=False):
        out = (output / f'{name}.out').open('w')
        err = subprocess.PIPE if ready else (output / f'{name}.err').open('w')
        files.append(out)
        if not ready:
            files.append(err)
        process = subprocess.Popen([str(binary / executable), *arguments], stdout=out, stderr=err, text=True)
        processes.append((name, process))
        return process

    def wait_ack(name, count):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if (output / f'{name}.out').read_text().count('report=accepted') >= count:
                return
            for _, process in processes:
                if process.poll() not in (None, 0):
                    raise RuntimeError('Process failed; inspect ' + str(output))
            time.sleep(0.01)
        raise RuntimeError('Order ACK deadline expired; inspect ' + str(output))

    try:
        exchange = spawn('exchange', 'exchange_server', ['--tcp-port', '0', '--snapshot-ms', '100', *multicast], ready=True)
        # The exchange announces readiness only after all three workers start.
        ready = exchange.stderr.readline()
        port = re.search(r'tcp_port=(\d+)', ready)
        if not port:
            raise RuntimeError('Exchange failed to start: ' + ready)
        common = ['--tcp-port', port[1], *multicast, '--interval-ms', '20']
        seed = spawn('liquidity', 'trading_client', [*common, '--client-id', '1', '--orders',
            str(ROOT / 'examples/liquidity.orders'), '--run-ms', '10000'])
        wait_ack('liquidity', 2)
        maker = spawn('maker', 'trading_client', [*common, '--client-id', '2', '--strategy', 'maker', '--run-ms', '1800'])
        taker = spawn('taker', 'trading_client', [*common, '--client-id', '3', '--strategy', 'taker',
            '--trade-ratio', '0.15', '--clip', '5', '--run-ms', '1800'])
        time.sleep(0.2)
        flow = spawn('aggressor', 'trading_client', [*common, '--client-id', '4', '--orders',
            str(ROOT / 'examples/aggressor.orders'), '--run-ms', '800'])
        for process in (flow, maker, taker):
            if process.wait(timeout=10):
                raise RuntimeError('Client failed; inspect ' + str(output))
        seed.send_signal(signal.SIGTERM)
        if seed.wait(timeout=5):
            raise RuntimeError('Liquidity client failed; inspect ' + str(output))
        exchange.send_signal(signal.SIGTERM)
        _, error = exchange.communicate(timeout=5)
        (output / 'exchange.err').write_text(ready + error)
        if exchange.returncode:
            raise RuntimeError('Exchange failed; inspect ' + str(output))
        for name, _ in processes:
            print(f'{name}:')
            for line in (output / f'{name}.out').read_text().splitlines():
                if line.startswith(('summary ', 'position ', 'requests=')):
                    print('  ' + line)
        print(f'Logs: {output}')
    finally:
        for _, process in processes:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
            if process.stderr and hasattr(process.stderr, 'close'):
                process.stderr.close()
        for file in files:
            file.close()


if __name__ == '__main__':
    main()
