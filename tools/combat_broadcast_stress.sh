#!/usr/bin/env bash
# Cross-thread broadcast stress: runs the scripted combat smoke N times in a row
# on one port. Each iteration creates an account, starts a PvE match, runs a
# battle (workers publishing at 10 Hz), and disconnects — exercising the
# Loop::defer publish path and match lifecycle churn.
#
# Run against the normal binary, or against the TSan build to catch unsynchronized
# uWS access:
#   bash tools/combat_broadcast_stress.sh 30 3460
#   SERVER_BIN=server/build-tsan/server TSAN=1 bash tools/combat_broadcast_stress.sh 5 3461
#
# Usage: bash tools/combat_broadcast_stress.sh [ITERATIONS] [PORT]
# Exit:  0 all iterations passed, 2 a failure.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
N="${1:-20}"
PORT="${2:-3460}"
SERVER_BIN="${SERVER_BIN:-$ROOT/server/build/server}"
LOG_DIR="${TMPDIR:-/tmp}/combat_stress_${PORT}"
mkdir -p "$LOG_DIR"

echo "Broadcast stress: $N iterations, port $PORT, server=$SERVER_BIN${TSAN:+ (TSan)}"

for i in $(seq 1 "$N"); do
    if ! SERVER_BIN="$SERVER_BIN" "$ROOT/tools/combat_smoke.sh" "$PORT" >"$LOG_DIR/iter_$i.log" 2>&1; then
        echo "Broadcast stress FAILED on iteration $i" >&2
        tail -40 "$LOG_DIR/iter_$i.log" >&2
        exit 2
    fi
    if [ "${TSAN:-0}" = "1" ] && grep -q "WARNING: ThreadSanitizer" "$LOG_DIR/iter_$i.log"; then
        echo "Broadcast stress: ThreadSanitizer report on iteration $i" >&2
        grep -A 30 "WARNING: ThreadSanitizer" "$LOG_DIR/iter_$i.log" >&2
        exit 2
    fi
    echo "  iteration $i/$N ok"
done

echo "Broadcast stress PASSED ($N iterations)"
exit 0
