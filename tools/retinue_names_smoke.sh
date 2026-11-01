#!/usr/bin/env bash
# Retinue name-localization smoke test harness.
#
# Starts the server with a temporary text directory containing English and
# Spanish fake name pools, then runs the Node flow (es market -> hire -> switch
# to en -> verify fixed name -> missing-language fallback).
#
# Usage: ./tools/retinue_names_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = build/start failed, 2 = smoke failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3401}"
GAME_DIR="$ROOT/game"
TEST_DIR="$ROOT/test_run/retinue_names_${PORT}"
TEXT_DIR="$TEST_DIR/text"
SERVER_BIN="${SERVER_BIN:-$ROOT/server/build/server}"

source "$ROOT/tools/postgres_test_env.sh"
postgres_test_env_init "ravenest_retinue_names_${PORT}"

if [ ! -x "$SERVER_BIN" ]; then
    echo "Building server…"
    (cd "$ROOT" && ./compile_server.sh -q >/dev/null 2>&1) || {
        echo "Server build failed" >&2
        exit 1
    }
fi

rm -rf "$TEST_DIR"
mkdir -p "$TEXT_DIR/en" "$TEXT_DIR/es"

# The market only unlocks archer/spearman at manor level 1. Give both languages
# distinct pools so the assertions can tell which one was used.
for class in archer spearman; do
    for sex in male female; do
        source_file="$GAME_DIR/text/en/names_${class}_${sex}.txt"
        cp "$source_file" "$TEXT_DIR/en/names_${class}_${sex}.txt"
        sed 's/^/ES /' "$source_file" > "$TEXT_DIR/es/names_${class}_${sex}.txt"
    done
done

(
    cd "$TEST_DIR" && "$SERVER_BIN" --port "$PORT" --content "$GAME_DIR" --text-dir "$TEXT_DIR"
) &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; postgres_test_env_cleanup; rm -rf "$TEST_DIR"' EXIT

sleep 1.5
if ! kill -0 "$SRV_PID" 2>/dev/null; then
    echo "Retinue names smoke: server failed to start" >&2
    exit 1
fi

for _ in $(seq 1 80); do
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        break
    fi
    sleep 0.25
done

if node "$ROOT/tools/retinue_names_smoke.mjs" --port "$PORT" --db-url "$RAVENEST_GAME_DB_URL"; then
    echo "Retinue names smoke PASSED"
    exit 0
fi
echo "Retinue names smoke FAILED" >&2
exit 2
