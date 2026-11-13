#!/usr/bin/env bash
# Config reload smoke test harness.
#
# Starts the server on an isolated port + scratch database with a writable copy
# of game/config, then runs tools/config_reload_smoke.mjs. The copy lets the
# test edit and corrupt configs without touching the repository.
#
# Usage: ./tools/config_reload_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3394}"
GAME_DIR="$ROOT/game"
RUN_DIR="$ROOT/test_run/config_reload_${PORT}"
CONTENT_DIR="$RUN_DIR/game"
DB_DIR="$RUN_DIR/db"
SERVER_BIN="${SERVER_BIN:-$ROOT/server/build/server}"

source "$ROOT/tools/postgres_test_env.sh"
postgres_test_env_init "ravenest_config_reload_${PORT}"

if [ ! -x "$SERVER_BIN" ]; then
    echo "Building server…"
    (cd "$ROOT" && ./compile_server.sh -q >/dev/null 2>&1) || {
        echo "Server build failed" >&2
        exit 1
    }
fi

rm -rf "$RUN_DIR"
mkdir -p "$CONTENT_DIR" "$DB_DIR"

# Copy config so the test can mutate it; link the read-only asset trees.
cp -a "$GAME_DIR/config" "$CONTENT_DIR/config"
ln -s "$GAME_DIR/images" "$CONTENT_DIR/images"
ln -s "$GAME_DIR/text" "$CONTENT_DIR/text"

( cd "$DB_DIR" && "$SERVER_BIN" --port "$PORT" --content "$CONTENT_DIR" ) \
    >"$RUN_DIR/server.log" 2>&1 &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; postgres_test_env_cleanup; rm -rf "$RUN_DIR"' EXIT

sleep 1.5
if ! kill -0 "$SRV_PID" 2>/dev/null; then
    echo "Config reload smoke: server failed to start" >&2
    tail -40 "$RUN_DIR/server.log" >&2 || true
    exit 1
fi

# Wait until the HTTP port accepts connections.
for _ in $(seq 1 80); do
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        break
    fi
    sleep 0.25
done

if node "$ROOT/tools/config_reload_smoke.mjs" \
    --port "$PORT" --pid "$SRV_PID" --content "$CONTENT_DIR" \
    --log "$RUN_DIR/server.log"; then
    echo "Config reload smoke PASSED"
    exit 0
fi

echo "Config reload smoke FAILED" >&2
echo "--- server log (tail) ---" >&2
tail -80 "$RUN_DIR/server.log" >&2 || true
exit 2
