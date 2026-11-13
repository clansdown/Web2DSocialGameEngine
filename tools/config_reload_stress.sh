#!/usr/bin/env bash
# ThreadSanitizer config reload stress test.
#
# Builds the TSan server if needed, starts it with a writable copy of
# game/config, then runs 8 concurrent config readers while sending SIGHUP at
# 10 Hz. Fails on any TSan report or request error.
#
# Usage: ./tools/config_reload_stress.sh [PORT] [SECONDS]
# Exit codes: 0 = pass, 1 = build/start failed, 2 = stress test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3395}"
SECONDS_TO_RUN="${2:-6}"
GAME_DIR="$ROOT/game"
RUN_DIR="$ROOT/test_run/config_reload_stress_${PORT}"
CONTENT_DIR="$RUN_DIR/game"
DB_DIR="$RUN_DIR/db"
SERVER_BIN="${SERVER_BIN:-$ROOT/server/build-tsan/server}"

source "$ROOT/tools/postgres_test_env.sh"
postgres_test_env_init "ravenest_config_reload_stress_${PORT}"

if [ ! -x "$SERVER_BIN" ]; then
    echo "Building ThreadSanitizer server…"
    (cd "$ROOT" && ./compile_debug-server.sh --tsan >/dev/null 2>&1) || {
        echo "TSan server build failed" >&2
        exit 1
    }
fi

rm -rf "$RUN_DIR"
mkdir -p "$CONTENT_DIR" "$DB_DIR"

cp -a "$GAME_DIR/config" "$CONTENT_DIR/config"
ln -s "$GAME_DIR/images" "$CONTENT_DIR/images"
ln -s "$GAME_DIR/text" "$CONTENT_DIR/text"

TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=0:exitcode=0}" \
    bash -c 'cd "$1" && exec "$2" --port "$3" --content "$4"' \
    _ "$DB_DIR" "$SERVER_BIN" "$PORT" "$CONTENT_DIR" \
    >"$RUN_DIR/server.log" 2>&1 &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; postgres_test_env_cleanup; rm -rf "$RUN_DIR"' EXIT

sleep 2
if ! kill -0 "$SRV_PID" 2>/dev/null; then
    echo "Config reload stress: server failed to start" >&2
    tail -60 "$RUN_DIR/server.log" >&2 || true
    exit 1
fi

for _ in $(seq 1 120); do
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        break
    fi
    sleep 0.25
done

if ! node "$ROOT/tools/config_reload_stress.mjs" \
    --port "$PORT" --pid "$SRV_PID" --content "$CONTENT_DIR" --seconds "$SECONDS_TO_RUN"; then
    echo "Config reload stress FAILED" >&2
    tail -80 "$RUN_DIR/server.log" >&2 || true
    exit 2
fi

if grep -q 'ThreadSanitizer' "$RUN_DIR/server.log"; then
    echo "Config reload stress FAILED: ThreadSanitizer report" >&2
    grep -A 30 -B 2 'ThreadSanitizer' "$RUN_DIR/server.log" >&2 || true
    exit 2
fi

echo "Config reload stress PASSED"
exit 0
