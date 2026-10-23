#!/usr/bin/env bash
# Translation smoke test — verifies the game-side translation seam end to end.
#
# Starts the translation sidecar in --passthrough mode (deterministic output, no
# multi-GB model) and the game server on a scratch DB, then exercises
# requestTranslations and asserts a translation is produced and stored.
#
# Usage: ./tools/translation_smoke.sh [SERVER_PORT] [SIDECAR_PORT]
# Exit codes: 0 = pass, 1 = server/sidecar build-or-start failed, 2 = smoke failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3394}"
SIDECAR_PORT="${2:-8790}"
GAME_DIR="$ROOT/game"
DB_DIR="$ROOT/.translation_smoke_db_${PORT}"
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

python3 "$ROOT/server/translation/sidecar.py" --port "$SIDECAR_PORT" --passthrough &
SIDECAR_PID=$!
( cd "$GAME_DIR" && "$SERVER_BIN" --port "$PORT" --db-dir "$DB_DIR" ) &
SRV_PID=$!
trap 'kill $SRV_PID $SIDECAR_PID 2>/dev/null; wait $SRV_PID $SIDECAR_PID 2>/dev/null; rm -rf "$DB_DIR"' EXIT

sleep 0.3
ready=0
for _ in $(seq 1 200); do
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "Translation smoke: server failed to start" >&2
        exit 1
    fi
    if bash -c "exec 3<>/dev/tcp/127.0.0.1/$PORT" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 0.1
done
if [ "$ready" -ne 1 ]; then
    echo "Translation smoke: server did not become ready" >&2
    exit 1
fi

if node "$ROOT/tools/translation_smoke.mjs" --port "$PORT"; then
    echo "Translation smoke PASSED"
    exit 0
fi
echo "Translation smoke FAILED" >&2
exit 2
