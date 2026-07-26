#!/usr/bin/env bash
set -euo pipefail

binary="${1:-build/release/replay_order_book}"
data_file="${2:-data/events.csv}"
output="${3:-results/perf.data}"

mkdir -p "$(dirname "$output")"
perf record -F 199 -g --call-graph fp -o "$output" -- "$binary" "$data_file"
echo "Open with: perf report -i $output"
