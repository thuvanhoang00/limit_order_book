#!/usr/bin/env bash
set -euo pipefail

case "${1:-}" in
    -h|--help)
        printf 'Usage: ./tools/run_tests.sh [debug|asan|tsan ...]\nDefault: debug and asan (ASan/UBSan), followed by Python reporting tests.\nBuilds and runs correctness/network/recovery tests. LOB_BUILD_JOBS defaults to 4.\n'
        exit 0
        ;;
esac
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir/.."
if (( $# == 0 )); then
    presets=(debug asan)
else
    presets=("$@")
fi

for preset in "${presets[@]}"; do
    cmake --preset "$preset" -DORDER_BOOK_BUILD_TESTS=ON \
        -DORDER_BOOK_BUILD_NETWORKING=ON -DORDER_BOOK_BUILD_BENCHMARKS=OFF
    cmake --build --preset "$preset" --parallel "${LOB_BUILD_JOBS:-4}"
    cmake --build --preset "$preset" --target check
done
python3 -m unittest discover -s tests/tools -v
