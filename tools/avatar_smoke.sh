#!/usr/bin/env bash
# Avatar storage smoke test (local backend).
#
# Starts the server on an isolated port + scratch database, then exercises
# upload → login fields → age-gated fetch → cooldown. R2 mode is covered by the
# same logic with RAVENEST_AVATAR_STORAGE=r2 plus a test bucket.
#
# Usage: bash tools/avatar_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3393}"
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

AVATAR_STORAGE="$(printf '%s' "${RAVENEST_AVATAR_STORAGE:-local}" | tr '[:upper:]' '[:lower:]')"
(
    cd "$DB_DIR" &&
    RAVENEST_AVATAR_STORAGE="$AVATAR_STORAGE" RAVENEST_TEST_ADULT_ACCOUNTS=1 \
        "$SERVER_BIN" --port "$PORT" --content "$GAME_DIR" --test-timeout-seconds 600
) &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; rm -rf "$DB_DIR"' EXIT

for _ in $(seq 1 80); do
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "Avatar smoke: server failed to start" >&2
        exit 1
    fi
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        break
    fi
    sleep 0.25
done

if AVATAR_EXPECT_STORAGE="$AVATAR_STORAGE" \
    node "$ROOT/tools/avatar_smoke.mjs" --port "$PORT"; then
    echo "Avatar smoke PASSED"
    exit 0
fi
echo "Avatar smoke FAILED" >&2
exit 2
