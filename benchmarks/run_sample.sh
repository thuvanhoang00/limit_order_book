#!/usr/bin/env bash
# Build, verify and benchmark the committed native AAPL session.
set -euo pipefail

case "${1:-}" in
    -h|--help)
        cat <<'HELP'
Usage: ./benchmarks/run_sample.sh [benchmark options]

Build current C++ sources, verify the committed native AAPL session, and generate
a standalone HTML performance report. No source/configuration edits are needed.

Default workflow:
  1. Configure/build Release in build/sample-release (no external test libraries).
  2. Verify dataset SHA-256, session counts and three FIFO checkpoints.
  3. Warm up with 100,000 records, then run 10 alternating off/on pairs (20 replays).
  4. Print the HTML report path; retain raw JSON and build/source metadata.

Examples:
  ./benchmarks/run_sample.sh
  ./benchmarks/run_sample.sh --output-dir results/aapl-before
  # Edit C++, then rebuild and measure the candidate:
  ./benchmarks/run_sample.sh --output-dir results/aapl-after
  ./benchmarks/run_sample.sh --cpu 2 --repetitions 20

Options (passed to tools/run_feed_benchmarks.py):
  --output-dir DIR       New artifact directory; existing results are preserved.
  --cpu N                Pin replay to an available CPU (default: first allowed CPU).
  --repetitions N        Number of off/on pairs (default: 10).
  --warmup-records N     Prefix warmup records (default: 100000).
  -h, --help             Show this guide and exit before building or benchmarking.

Environment defaults:
  LOB_BENCH_CPU, LOB_BENCH_REPETITIONS, LOB_BUILD_JOBS (default build jobs: 4).

Requirements: Linux, a C++20 compiler, CMake >= 3.20, Ninja, Python 3, zlib and GNU time.
Ubuntu/Debian packages: build-essential cmake ninja-build python3 zlib1g-dev time.

Scope: native feed parser -> routing -> order book. Compare before/after reports
using the same dataset/build/CPU settings. Off/on measures observer cost, separate
from implementation speedup. Risk/TCP/journal require the pipeline benchmark.
See data/README.md and docs/pipeline_benchmarks.md for details.
HELP
        exit 0
        ;;
esac

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

for dependency in cmake ninja python3; do
    if ! command -v "$dependency" >/dev/null 2>&1; then
        printf 'Missing required tool: %s\n' "$dependency" >&2
        printf 'Ubuntu/Debian packages: build-essential cmake ninja-build python3 zlib1g-dev time\n' >&2
        exit 1
    fi
done
if [[ ! -x /usr/bin/time ]]; then
    printf 'GNU time is required at /usr/bin/time (Ubuntu/Debian package: time).\n' >&2
    exit 1
fi

sample_build_dir="$repo_root/build/sample-release"
sample_jobs="${LOB_BUILD_JOBS:-4}"
sample_cpu="${LOB_BENCH_CPU:-$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')}"
sample_repetitions="${LOB_BENCH_REPETITIONS:-10}"

cmake -S "$repo_root" -B "$sample_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DORDER_BOOK_BUILD_TESTS=OFF -DORDER_BOOK_BUILD_BENCHMARKS=OFF \
    -DORDER_BOOK_BUILD_NETWORKING=OFF -DORDER_BOOK_FETCH_DEPS=OFF
cmake --build "$sample_build_dir" --parallel "$sample_jobs" --target replay_market_data
python3 tools/verify_itch_sample.py --bin-dir "$sample_build_dir"
python3 tools/run_feed_benchmarks.py \
    --bin-dir "$sample_build_dir" \
    --input data/samples/nasdaq-20190130-AAPL.itch.gz --symbol AAPL \
    --compare-instrumentation --repetitions "$sample_repetitions" \
    --warmup-records 100000 --cpu "$sample_cpu" "$@"
