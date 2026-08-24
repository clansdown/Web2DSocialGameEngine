#!/usr/bin/env bash
#
# Convenience wrapper that delegates to build_analyzer.sh.
# See build_analyzer.sh for options and exit codes.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$SCRIPT_DIR/build_analyzer.sh" "$@"
