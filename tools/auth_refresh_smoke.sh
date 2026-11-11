#!/usr/bin/env bash
# Auth refresh/logout smoke test.
#
# Starts the server on an isolated port + scratch database, then exercises the
# documented /api/auth/refresh and /api/auth/logout routes:
#   no cookie -> invalid_refresh_token (never token_missing)
#   valid cookie -> 200 + new access token + refreshed Set-Cookie
#   bogus cookie -> invalid_refresh_token
#   logout -> clearing Set-Cookie, then the old cookie is invalid
#
# Usage: bash tools/auth_refresh_smoke.sh [PORT]
# Exit codes: 0 = pass, 1 = server build/start failed, 2 = smoke test failed
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-3398}"
BASE_DIR="$ROOT/test_run/auth_smoke_${PORT}"
CONTENT="$BASE_DIR/content"
DB_DIR="$BASE_DIR/db"
SERVER_BIN="${SERVER_BIN:-$ROOT/server/build/server}"

source "$ROOT/tools/postgres_test_env.sh"
postgres_test_env_init "ravenest_auth_${PORT}" || exit 1
trap 'postgres_test_env_cleanup' EXIT

if [ ! -x "$SERVER_BIN" ]; then
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

# Keep the smoke test self-contained: account creation must not depend on the
# deployment invite configuration.
cat > "$CONTENT/config/registration.json" <<'EOF'
{
  "mode": "off",
  "global_code": "",
  "players_can_generate": false,
  "player_daily_limit": 0,
  "player_lifetime_limit": 0,
  "create_account_rate_limit": {
    "enabled": true,
    "window_seconds": 3600,
    "max_attempts_per_network": 300,
    "ipv6_prefix_bits": 64,
    "max_entries": 10000,
    "prune_interval_seconds": 60
  }
}
EOF

(
    cd "$DB_DIR" &&
    exec "$SERVER_BIN" --port "$PORT" --content "$CONTENT" --test-timeout-seconds 600
) >"$DB_DIR/server.log" 2>&1 &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; postgres_test_env_cleanup; rm -rf "$BASE_DIR"' EXIT

READY=0
for _ in $(seq 1 80); do
    if ! kill -0 "$SRV_PID" 2>/dev/null; then
        echo "Auth smoke: server failed to start" >&2
        cat "$DB_DIR/server.log" >&2
        exit 1
    fi
    if (exec 3<>/dev/tcp/127.0.0.1/"$PORT") 2>/dev/null; then
        exec 3>&-
        READY=1
        break
    fi
    sleep 0.25
done
if [ "$READY" -ne 1 ]; then
    echo "Auth smoke: server did not become ready" >&2
    cat "$DB_DIR/server.log" >&2
    exit 1
fi

if node "$ROOT/tools/auth_refresh_smoke.mjs" --port "$PORT"; then
    echo "Auth refresh smoke PASSED"
    exit 0
fi
echo "Auth refresh smoke FAILED" >&2
cat "$DB_DIR/server.log" >&2
exit 2
