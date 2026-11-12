#!/usr/bin/env bash
# Profile identity smoke test.
#
# Starts the server on an isolated port + scratch database, then exercises the
# safe-name and adult custom-name editors, including validation and the
# safe-name-follows-visible rule. Adult creation uses the test-only hook.
#
# Usage: bash tools/profile_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3395}"
GAME_DIR="$ROOT/game"
DB_DIR="$ROOT/test_run/smoke_${PORT}"
SERVER_BIN="${SERVER_BIN:-$ROOT/server/build/server}"

source "$ROOT/tools/postgres_test_env.sh"
postgres_test_env_init "ravenest_profile_${PORT}"

if [ ! -x "$SERVER_BIN" ]; then
    echo "Building server…"
    (cd "$ROOT" && ./compile_server.sh -q >/dev/null 2>&1) || {
        echo "Server build failed" >&2
        exit 1
    }
fi

mkdir -p "$DB_DIR"

(
    cd "$DB_DIR" &&
    RAVENEST_TEST_ADULT_ACCOUNTS=1 \
        "$SERVER_BIN" --port "$PORT" --content "$GAME_DIR" --test-timeout-seconds 600
) &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; postgres_test_env_cleanup; rm -rf "$DB_DIR"' EXIT

for _ in $(seq 1 80); do
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "Profile smoke: server failed to start" >&2
        exit 1
    fi
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        break
    fi
    sleep 0.25
done

if node "$ROOT/tools/profile_smoke.mjs" --port "$PORT"; then
    echo "Profile smoke PASSED"
    exit 0
fi
echo "Profile smoke FAILED" >&2
exit 2
