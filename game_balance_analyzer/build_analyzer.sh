#!/usr/bin/env bash
#
# Build the game_balance_analyzer tool.
#
# Usage:
#   ./build_analyzer.sh          Configure + build (default, normal output)
#   ./build_analyzer.sh -q       Quiet mode (errors only)
#   ./build_analyzer.sh -v       Verbose output
#   ./build_analyzer.sh -c       Clean the build directory first (fresh configure)
#   ./build_analyzer.sh --help   Show this help
#
# Exit codes:
#   0  Build succeeded
#   1  Configure or build failed
#
# The binary is produced at build/game_balance_analyzer.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
CMAKE="${CMAKE:-cmake}"

QUIET=0
VERBOSE=0
CLEAN=0

usage() {
    cat <<'EOF'
Build the game_balance_analyzer tool.

Usage:
  ./build_analyzer.sh          Configure + build (default, normal output)
  ./build_analyzer.sh -q       Quiet mode (errors only)
  ./build_analyzer.sh -v       Verbose output
  ./build_analyzer.sh -c       Clean the build directory first
  ./build_analyzer.sh --help   Show this help

Exit codes:
  0  Build succeeded
  1  Configure or build failed

The binary is produced at build/game_balance_analyzer.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -q) QUIET=1 ;;
        -v) VERBOSE=1 ;;
        -c) CLEAN=1 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
    shift
done

if [[ "$CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
    rm -rf "$BUILD_DIR"
fi

if [[ "$QUIET" -eq 1 ]]; then
    "$CMAKE" -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
    "$CMAKE" --build "$BUILD_DIR" --config Release >/dev/null
else
    echo "Configuring game_balance_analyzer..."
    "$CMAKE" -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    echo "Building game_balance_analyzer..."
    if [[ "$VERBOSE" -eq 1 ]]; then
        "$CMAKE" --build "$BUILD_DIR" --config Release --verbose
    else
        "$CMAKE" --build "$BUILD_DIR" --config Release
    fi
fi

if [[ -x "$BUILD_DIR/game_balance_analyzer" ]]; then
    echo "Build successful: $BUILD_DIR/game_balance_analyzer"
    exit 0
fi

echo "Build failed: binary not found at $BUILD_DIR/game_balance_analyzer" >&2
exit 1
