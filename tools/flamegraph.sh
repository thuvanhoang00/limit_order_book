#!/usr/bin/env bash
set -euo pipefail

perf_data="${1:-results/perf.data}"
output_svg="${2:-results/flamegraph.svg}"

: "${FLAMEGRAPH_DIR:?Set FLAMEGRAPH_DIR to Brendan Gregg's FlameGraph repository}"
mkdir -p "$(dirname "$output_svg")"

perf script -i "$perf_data" \
  | "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" \
  | "$FLAMEGRAPH_DIR/flamegraph.pl" \
  > "$output_svg"

echo "Generated $output_svg"
