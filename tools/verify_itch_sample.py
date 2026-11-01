"""Verify the committed native ITCH sample on another machine (stdlib only)."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def snapshot_hash(snapshot):
    encoded = json.dumps(snapshot, sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(encoded).hexdigest()


def verify(manifest_path, bin_dir):
    manifest_path = Path(manifest_path).resolve()
    manifest = json.loads(manifest_path.read_text())
    sample = manifest_path.parent / manifest['file']
    with sample.open('rb') as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    if digest.hexdigest() != manifest['sha256'] or sample.stat().st_size != manifest['bytes']:
        raise ValueError('Sample checksum/size differs from the committed manifest')
    binary = Path(bin_dir).resolve() / 'replay_market_data'
    with tempfile.TemporaryDirectory(prefix='lob-sample-check-') as temp:
        snapshot = Path(temp) / 'snapshot.json'
        def replay(limit=0):
            command = [str(binary), '--input', str(sample), '--symbol', manifest['symbol'],
                       '--pool-bytes', str(manifest['pool_bytes']), '--max-records', str(limit),
                       '--snapshot', str(snapshot)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            if result.returncode:
                raise ValueError('Replay failed: ' + result.stderr.strip())
            return json.loads(result.stdout), snapshot_hash(json.loads(snapshot.read_text()))

        result, state = replay()
        for key, expected in manifest['expected_replay'].items():
            if result.get(key) != expected:
                raise ValueError(f'{key}: expected {expected!r}, got {result.get(key)!r}')
        if state != manifest['final_snapshot_sha256']:
            raise ValueError('Final FIFO snapshot differs')
        for checkpoint in manifest['checkpoints']:
            prefix, state = replay(checkpoint['records'])
            if prefix['records'] != checkpoint['records'] or not prefix['limited'] or \
                    prefix['complete_input'] or prefix['session_ended'] or not prefix['invariants_valid']:
                raise ValueError('Checkpoint did not produce the expected valid limited replay')
            if prefix['book_updates'] != checkpoint['book_updates'] or state != checkpoint['snapshot_sha256']:
                raise ValueError(f'FIFO checkpoint at {checkpoint["records"]} records differs')
    return dict(verified=True, file=manifest['file'], sha256=manifest['sha256'],
                records=result['records'], book_updates=result['book_updates'],
                fifo_checkpoints=len(manifest['checkpoints']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=Path, default=ROOT / 'build/release')
    parser.add_argument('--manifest', type=Path,
                        default=ROOT / 'data/samples/nasdaq-20190130-AAPL.manifest.json')
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.manifest, args.bin_dir), indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Sample verification failed: {error}\n')


if __name__ == '__main__':
    main()
