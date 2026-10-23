#!/usr/bin/env bash
# Determinism replay check (rts_design.md D27 / Milestone 10).
#
# Runs the scripted combat smoke once with a fixed --combat-seed and
# --combat-replay-dir, then re-runs the recorded tick-exact command log through
# the server's `--combat-replay` mode and asserts the final state_hash matches.
#
# Usage: ./tools/replay_combat.sh [SEED] [PORT]
# Exit codes: 0 = deterministic, 2 = mismatch/failure, 1 = setup failure
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="${1:-12345}"
PORT="${2:-3401}"
REPLAY_DIR="$ROOT/test_run/replays_${PORT}"
SERVER_BIN="$ROOT/server/build/server"

if [ ! -x "$SERVER_BIN" ]; then
    echo "Building server…"
    (cd "$ROOT" && ./compile_server.sh -q >/dev/null 2>&1) || {
        echo "Server build failed" >&2
        exit 1
    }
fi

rm -rf "$REPLAY_DIR"
mkdir -p "$REPLAY_DIR"

echo "Recording a match (seed=$SEED)…"
if ! SMOKE_SEED="$SEED" SMOKE_REPLAY_DIR="$REPLAY_DIR" \
        "$ROOT/tools/combat_smoke.sh" "$PORT" >"$REPLAY_DIR/smoke.out" 2>&1; then
    echo "Replay: smoke run failed; tail of output:" >&2
    tail -20 "$REPLAY_DIR/smoke.out" >&2
    exit 2
fi

FILE="$(ls -1 "$REPLAY_DIR"/*.json 2>/dev/null | head -1)"
if [ -z "$FILE" ]; then
    echo "Replay: no recorded replay file was written" >&2
    exit 2
fi

echo "Replaying $FILE…"
mkdir -p "$REPLAY_DIR/db"
( cd "$REPLAY_DIR/db" && "$SERVER_BIN" --content "$ROOT/game" --combat-replay "$FILE" ) \
    > "$REPLAY_DIR/replay.out" 2>&1
RC=$?
cat "$REPLAY_DIR/replay.out"

if [ "$RC" -ne 0 ]; then
    echo "Replay determinism FAILED (exit $RC)" >&2
    exit 2
fi
echo "Replay determinism PASSED (seed=$SEED)"
exit 0
