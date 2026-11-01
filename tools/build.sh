#!/usr/bin/env bash
set -euo pipefail

case "${1:-}" in
    -h|--help)
        printf 'Usage: ./tools/build.sh [release|debug|asan|tsan]\nBuild from current C++ sources. Default: release. LOB_BUILD_JOBS defaults to 4.\n'
        exit 0
        ;;
esac
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir/.."
preset="${1:-release}"
cmake --preset "$preset"
cmake --build --preset "$preset" --parallel "${LOB_BUILD_JOBS:-4}"
