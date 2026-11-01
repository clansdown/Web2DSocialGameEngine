#!/usr/bin/env bash
# Clears weeding progress and sessions for all characters.
# Usage: ./reset_weeding_for_testing.sh [db_url]
set -euo pipefail
DB_URL="${1:-${RAVENEST_GAME_DB_URL:-}}"
if [ -z "$DB_URL" ]; then
  echo "Error: set RAVENEST_GAME_DB_URL or pass the database URL" >&2
  exit 1
fi
psql "$DB_URL" -v ON_ERROR_STOP=1 <<'SQL'
DELETE FROM mini_game_progress WHERE mini_game = 'weeding';
DELETE FROM weeding_sessions;
UPDATE player_game_state
SET current_mini_game = NULL, current_level_id = NULL
WHERE current_mini_game = 'weeding';
SQL
echo "Weeding progress cleared."
