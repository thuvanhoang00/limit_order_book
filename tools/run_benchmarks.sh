#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

preset=release
output_root=results
if (( $# > 0 )) && [[ "$1" != --* ]]; then
    preset="$1"
    shift
fi
if (( $# > 0 )) && [[ "$1" != --* ]]; then
    output_root="$1"
    shift
fi

exec python3 "$script_dir/benchmark_report.py" run \
    --preset "$preset" --output-root "$output_root" "$@"
