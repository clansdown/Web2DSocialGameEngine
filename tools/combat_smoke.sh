#!/usr/bin/env bash
# Combat WS smoke test harness — Milestone 1 verification.
#
# Starts the server on an isolated port + scratch database, runs the Node
# combat_smoke.mjs flow (combatGetConfigs → combatCreate → WS auth → ready →
# battle → match_ended), then tears everything down.
#
# Usage: ./tools/combat_smoke.sh [PORT]
# Optional env: SMOKE_OPPORTUNITY=<id> and SMOKE_AOE_CHECK=1 enabled by the
# node harness via --opportunity / --aoe-check. SMOKE_SEED=<n> passes a fixed
# --combat-seed to the server for deterministic runs (see replay_combat.sh).
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3390}"
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

SERVER_ARGS=(--port "$PORT" --content "$GAME_DIR")
if [ -n "${SMOKE_SEED:-}" ]; then SERVER_ARGS+=(--combat-seed "$SMOKE_SEED"); fi
if [ -n "${SMOKE_REPLAY_DIR:-}" ]; then SERVER_ARGS+=(--combat-replay-dir "$SMOKE_REPLAY_DIR"); fi

( cd "$DB_DIR" && "$SERVER_BIN" "${SERVER_ARGS[@]}" ) &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; rm -rf "$DB_DIR"' EXIT

sleep 1.5
if ! kill -0 "$SRV_PID" 2>/dev/null; then
    echo "Combat smoke: server failed to start" >&2
    exit 1
fi

# Wait until the HTTP port accepts connections — config/image/database init can
# take a moment, and a fixed sleep is flaky.
for _ in $(seq 1 80); do
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        break
    fi
    sleep 0.25
done

EXTRA=()
if [ -n "${SMOKE_OPPORTUNITY:-}" ]; then EXTRA+=(--opportunity "$SMOKE_OPPORTUNITY"); fi
if [ "${SMOKE_AOE_CHECK:-}" = "1" ]; then EXTRA+=(--aoe-check); fi

if node "$ROOT/tools/combat_smoke.mjs" --port "$PORT" "${EXTRA[@]+"${EXTRA[@]}"}"; then
    # Let the loop thread flush the deferred replay write (if enabled).
    sleep 0.5
    echo "Combat smoke PASSED"
    exit 0
fi
echo "Combat smoke FAILED" >&2
exit 2