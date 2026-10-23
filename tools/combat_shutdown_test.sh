#!/usr/bin/env bash
# Graceful-shutdown ordering test.
#
# Starts the server with --shutdown-after-ms so it initiates a uWS "fall-through"
# shutdown WHILE a combat battle is live (workers publishing), then asserts the
# server exits cleanly (exit 0) with no crash / sanitizer report. This is the
# test that exercises the producer-stop-before-App-destruction ordering.
#
# Usage: bash tools/combat_shutdown_test.sh [PORT]
# Env:   SERVER_BIN  override the binary (default server/build/server)
#        SHUTDOWN_MS graceful-shutdown delay (default 6000)
#        TSAN=1       expect the TSan binary and scan for reports
# Exit:  0 pass, 1 setup failure, 2 failure.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME_DIR="$ROOT/game"
PORT="${1:-3455}"
SHUTDOWN_MS="${SHUTDOWN_MS:-6000}"
SERVER_BIN="${SERVER_BIN:-$ROOT/server/build/server}"
DB_DIR="$ROOT/.combat_shutdown_db_${PORT}"
LOG="$ROOT/.combat_shutdown_${PORT}.log"

if [ ! -x "$SERVER_BIN" ]; then
    echo "Shutdown test: server binary not found at $SERVER_BIN (run ./compile_server.sh)" >&2
    exit 1
fi

rm -rf "$DB_DIR"
mkdir -p "$DB_DIR"

echo "Starting server (shutdown-after-ms=$SHUTDOWN_MS)…"
# Run from game/ so relative config/images paths resolve (same as combat_smoke.sh).
if [ "${TSAN:-0}" = "1" ]; then
    ( cd "$GAME_DIR" && TSAN_OPTIONS="halt_on_error=0" exec "$SERVER_BIN" \
        --port "$PORT" --db-dir "$DB_DIR" --combat-seed 4242 \
        --shutdown-after-ms "$SHUTDOWN_MS" ) >"$LOG" 2>&1 &
else
    ( cd "$GAME_DIR" && exec "$SERVER_BIN" \
        --port "$PORT" --db-dir "$DB_DIR" --combat-seed 4242 \
        --shutdown-after-ms "$SHUTDOWN_MS" ) >"$LOG" 2>&1 &
fi
SRV_PID=$!

cleanup() {
    kill "$SRV_PID" 2>/dev/null
    wait "$SRV_PID" 2>/dev/null
    rm -rf "$DB_DIR"
}
trap cleanup EXIT

sleep 1.5
if ! kill -0 "$SRV_PID" 2>/dev/null; then
    echo "Shutdown test: server failed to start" >&2
    tail -20 "$LOG" >&2
    exit 2
fi

echo "Driving a live battle until the server shuts itself down…"
node "$ROOT/tools/combat_shutdown_test.mjs" --port "$PORT"
NODE_RC=$?

# Wait (bounded) for the server to finish its fall-through teardown.
for _ in $(seq 1 400); do
    if ! kill -0 "$SRV_PID" 2>/dev/null; then break; fi
    sleep 0.1
done
if kill -0 "$SRV_PID" 2>/dev/null; then
    echo "Shutdown test: server did not exit within 40s" >&2
    tail -30 "$LOG" >&2
    exit 2
fi
wait "$SRV_PID"
SRV_RC=$?

echo "server exit code: $SRV_RC (node exit code: $NODE_RC)"

if [ -n "${TSAN:-}" ] && grep -q "WARNING: ThreadSanitizer" "$LOG"; then
    echo "Shutdown test: ThreadSanitizer report detected" >&2
    grep -A 30 "WARNING: ThreadSanitizer" "$LOG" >&2
    exit 2
fi

if [ "$SRV_RC" -ne 0 ]; then
    echo "Shutdown test FAILED: server did not exit cleanly" >&2
    tail -30 "$LOG" >&2
    exit 2
fi
if [ "$NODE_RC" -ne 0 ]; then
    echo "Shutdown test FAILED: client did not observe a clean server shutdown" >&2
    exit 2
fi

echo "Combat shutdown test PASSED"
exit 0
