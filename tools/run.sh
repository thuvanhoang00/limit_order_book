#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(dirname -- "$script_dir")"

preset="${1:-release}"
data_file="${2:-data/sample_events.csv}"

cd "$project_dir"
"$script_dir/build.sh" "$preset"
exec "$project_dir/build/$preset/replay_order_book" "$data_file"
