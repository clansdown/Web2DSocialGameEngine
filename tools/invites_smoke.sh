#!/usr/bin/env bash
# Invitation/registration smoke test harness.
#
# Exercises off -> invite_only -> global_code against one scratch database,
# then a separate rate-limit scenario. Uses a temporary content root so
# registration.json can be swapped without touching game/config.
#
# Usage: ./tools/invites_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = build/start failed, 2 = smoke failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3397}"
BASE_DIR="$ROOT/test_run/invites_smoke_${PORT}"
CONTENT="$BASE_DIR/content"
DB_DIR="$BASE_DIR/db"
STATE="$BASE_DIR/state.json"
SERVER_BIN="$ROOT/server/build/server"
STAFF_BIN="$ROOT/server/build/staff_admin"
GLOBAL_CODE="SMOKE-GLOBAL-9Q7X"

if [ ! -x "$SERVER_BIN" ] || [ ! -x "$STAFF_BIN" ]; then
    echo "Building server…"
    (cd "$ROOT" && ./compile_server.sh -q >/dev/null 2>&1) || {
        echo "Server build failed" >&2
        exit 1
    }
fi

rm -rf "$BASE_DIR"
mkdir -p "$DB_DIR" "$CONTENT"
cp -a "$ROOT/game/config" "$CONTENT/config"
ln -s "$ROOT/game/images" "$CONTENT/images"
ln -s "$ROOT/game/text" "$CONTENT/text"

write_config() {
    local mode="$1" global="$2" players="$3" daily="$4" lifetime="$5"
    local max_attempts="$6" max_entries="$7"
    cat > "$CONTENT/config/registration.json" <<EOF
{
  "mode": "${mode}",
  "global_code": "${global}",
  "players_can_generate": ${players},
  "player_daily_limit": ${daily},
  "player_lifetime_limit": ${lifetime},
  "create_account_rate_limit": {
    "enabled": true,
    "window_seconds": 3600,
    "max_attempts_per_network": ${max_attempts},
    "ipv6_prefix_bits": 64,
    "max_entries": ${max_entries},
    "prune_interval_seconds": 60
  }
}
EOF
}

SRV_PID=""
stop_server() {
    if [ -n "$SRV_PID" ]; then
        kill "$SRV_PID" 2>/dev/null
        wait "$SRV_PID" 2>/dev/null
        SRV_PID=""
    fi
}

cleanup() {
    stop_server
    rm -rf "$BASE_DIR"
}
trap cleanup EXIT

start_server() {
    ( cd "$DB_DIR" && exec "$SERVER_BIN" --port "$PORT" --content "$CONTENT" ) \
        >"$BASE_DIR/server.log" 2>&1 &
    SRV_PID=$!
    local ready=0
    for _ in $(seq 1 60); do
        if ! kill -0 "$SRV_PID" 2>/dev/null; then
            echo "Invites smoke: server failed to start" >&2
            cat "$BASE_DIR/server.log" >&2
            exit 1
        fi
        if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
            exec 3>&-
            ready=1
            break
        fi
        sleep 0.5
    done
    if [ "$ready" -ne 1 ]; then
        echo "Invites smoke: server did not become ready" >&2
        cat "$BASE_DIR/server.log" >&2
        exit 1
    fi
}

run_scenario() {
    local scenario="$1"
    echo "Invites smoke: scenario $scenario"
    node "$ROOT/tools/invites_smoke.mjs" \
        --port "$PORT" --db-dir "$DB_DIR" --state "$STATE" \
        --scenario "$scenario" --global-code "$GLOBAL_CODE"
}

# 1. off: create the bootstrap accounts; invites.db must stay absent.
write_config "off" "" false 0 0 300 10000
start_server
run_scenario "off" || { stop_server; echo "Invites smoke FAILED (off)" >&2; exit 2; }
stop_server

# 2. invite_only: staff bootstrap code, player generation/limits, staff undo.
write_config "invite_only" "" true 2 5 300 10000
start_server
run_scenario "invite_only" || { stop_server; echo "Invites smoke FAILED (invite_only)" >&2; exit 2; }
stop_server

# 3. global_code: shared code + a still-valid individual code from step 2.
write_config "global_code" "$GLOBAL_CODE" false 0 0 300 10000
start_server
run_scenario "global" || { stop_server; echo "Invites smoke FAILED (global_code)" >&2; exit 2; }
stop_server

# 4. rate limiter: one attempt per network, two networks in the LRU.
write_config "off" "" false 0 0 1 2
start_server
run_scenario "rate" || { stop_server; echo "Invites smoke FAILED (rate)" >&2; exit 2; }
stop_server

echo "Invites smoke PASSED"
exit 0
