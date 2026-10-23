#!/usr/bin/env bash
# Monetization smoke test harness — Milestone 11 verification.
#
# Starts the server on an isolated port + scratch database, runs the Node
# monetization_smoke.mjs flow (store → Platinum → purchase → idempotency →
# equip), then tears everything down.
#
# Usage: ./tools/monetization_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3392}"
GAME_DIR="$ROOT/game"
DB_DIR="$ROOT/test_run/smoke_${PORT}"
SERVER_BIN="$ROOT/server/build/server"

if [ ! -x "$SERVER_BIN" ]; then
    echo "Building server…"
    (cd "$ROOT" && ./compile_server.sh -q >/dev/null 2>&1) || {
        echo "Server build failed" >&2
        exit 1
    }
fi

rm -rf "$DB_DIR"
mkdir -p "$DB_DIR"

( cd "$DB_DIR" && "$SERVER_BIN" --port "$PORT" --content "$GAME_DIR" ) &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; rm -rf "$DB_DIR"' EXIT

sleep 1.5
if ! kill -0 "$SRV_PID" 2>/dev/null; then
    echo "Monetization smoke: server failed to start" >&2
    exit 1
fi

if node "$ROOT/tools/monetization_smoke.mjs" --port "$PORT"; then
    echo "Monetization smoke PASSED"
    exit 0
fi
echo "Monetization smoke FAILED" >&2
exit 2
