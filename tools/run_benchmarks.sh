#!/usr/bin/env bash
set -euo pipefail

preset="${1:-release}"
output_dir="${2:-results}"
mkdir -p "$output_dir"

cmake --preset "$preset"
cmake --build --preset "$preset" --parallel

"build/${preset}/order_book_benchmarks" \
  --benchmark_min_time=2s \
  --benchmark_repetitions=10 \
  --benchmark_report_aggregates_only=true \
  --benchmark_out="${output_dir}/benchmark.csv" \
  --benchmark_out_format=csv
