#!/usr/bin/env bash
# Reeve advisor smoke test harness.
#
# Starts the server on an isolated port + scratch database, runs the Node
# reeve_smoke.mjs flow (create account → seed a manor → getReeveReport →
# execute the recommended action → force a hold), then tears everything down.
#
# Usage: ./tools/reeve_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3399}"
GAME_DIR="$ROOT/game"
DB_DIR="$ROOT/test_run/smoke_${PORT}"
SERVER_BIN="${SERVER_BIN:-$ROOT/server/build/server}"

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
    echo "Reeve smoke: server failed to start" >&2
    exit 1
fi

# Wait until the HTTP port accepts connections.
for _ in $(seq 1 80); do
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        break
    fi
    sleep 0.25
done

if node "$ROOT/tools/reeve_smoke.mjs" --port "$PORT" --db "$DB_DIR/game.db"; then
    echo "Reeve smoke PASSED"
    exit 0
fi
echo "Reeve smoke FAILED" >&2
exit 2
