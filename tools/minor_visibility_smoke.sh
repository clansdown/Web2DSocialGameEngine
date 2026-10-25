#!/usr/bin/env bash
# Minor content-visibility smoke test.
#
# Starts the server on an isolated port + scratch database, then exercises the
# self-service minor path (safe catalogs, safe message authoring, adult-only
# groups, avatar age gates, and the removed public age override).
#
# Usage: bash tools/minor_visibility_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3394}"
GAME_DIR="$ROOT/game"
DB_DIR="$ROOT/test_run/minor_${PORT}"
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

(
    cd "$DB_DIR" &&
    "$SERVER_BIN" --port "$PORT" --content "$GAME_DIR"
) &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; rm -rf "$DB_DIR"' EXIT

for _ in $(seq 1 80); do
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "Minor visibility smoke: server failed to start" >&2
        exit 1
    fi
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        break
    fi
    sleep 0.25
done

if node "$ROOT/tools/minor_visibility_smoke.mjs" --port "$PORT"; then
    echo "Minor visibility smoke PASSED"
    exit 0
fi
echo "Minor visibility smoke FAILED" >&2
exit 2
