#!/usr/bin/env bash
set -euo pipefail

binary="${1:-build/release/replay_order_book}"
data_file="${2:-data/events.csv}"

perf stat -r 10 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations \
  "$binary" "$data_file"
