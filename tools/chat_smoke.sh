#!/usr/bin/env bash
# Chat smoke test harness — end-to-end verification of the chat subsystem.
#
# Starts the server on an isolated port + scratch database, runs the Node
# chat_smoke.mjs flow (DMs, poll, edit/delete, reactions, friends, groups,
# moderation, blocks, wildlands), then tears everything down.
#
# Usage: ./tools/chat_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3393}"
GAME_DIR="$ROOT/game"
DB_DIR="$ROOT/.chat_smoke_db_${PORT}"
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

( cd "$GAME_DIR" && RAVENEST_TEST_ADULT_ACCOUNTS=1 \
    "$SERVER_BIN" --port "$PORT" --db-dir "$DB_DIR" --test-timeout-seconds 600 ) &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; rm -rf "$DB_DIR"' EXIT

sleep 0.3
# Wait for the listener to accept connections (schema creation on a fresh DB can
# take >1.5s on the project filesystem, so a fixed sleep races startup).
ready=0
for _ in $(seq 1 200); do
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "Chat smoke: server failed to start" >&2
        exit 1
    fi
    if bash -c "exec 3<>/dev/tcp/127.0.0.1/$PORT" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 0.1
done
if [ "$ready" -ne 1 ]; then
    echo "Chat smoke: server did not become ready" >&2
    exit 1
fi

if node "$ROOT/tools/chat_smoke.mjs" --port "$PORT"; then
    echo "Chat smoke PASSED"
    exit 0
fi
echo "Chat smoke FAILED" >&2
exit 2
