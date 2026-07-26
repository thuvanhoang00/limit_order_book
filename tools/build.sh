#!/usr/bin/env bash
set -euo pipefail

preset="${1:-release}"
cmake --preset "$preset"
cmake --build --preset "$preset" --parallel
