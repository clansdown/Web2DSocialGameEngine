#!/usr/bin/env bash
# Staff/support smoke test harness.
#
# Starts the server on an isolated port + scratch databases, creates accounts,
# grants staff with the CLI, then runs tools/staff_smoke.mjs.
#
# Usage: ./tools/staff_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = build/start failed, 2 = smoke failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3393}"
GAME_DIR="$ROOT/game"
DB_DIR="$ROOT/test_run/staff_smoke_${PORT}"
SERVER_BIN="$ROOT/server/build/server"
STAFF_BIN="$ROOT/server/build/staff_admin"

if [ ! -x "$SERVER_BIN" ] || [ ! -x "$STAFF_BIN" ]; then
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

# Readiness poll: a cold scratch DB can take several seconds to initialize.
READY=0
for _ in $(seq 1 60); do
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "Staff smoke: server failed to start" >&2
        exit 1
    fi
    if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
        exec 3>&-
        READY=1
        break
    fi
    sleep 0.5
done
if [ "$READY" -ne 1 ]; then
    echo "Staff smoke: server did not become ready" >&2
    exit 1
fi

if node "$ROOT/tools/staff_smoke.mjs" --port "$PORT" --db-dir "$DB_DIR"; then
    echo "Staff smoke PASSED"
    exit 0
fi
echo "Staff smoke FAILED" >&2
exit 2
