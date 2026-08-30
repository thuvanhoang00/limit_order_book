#!/usr/bin/env bash
set -euo pipefail

if (( $# == 0 )); then
    presets=(debug asan)
else
    presets=("$@")
fi

for preset in "${presets[@]}"; do
    cmake --preset "$preset"
    cmake --build --preset "$preset" --parallel
    cmake --build --preset "$preset" --target check
done
