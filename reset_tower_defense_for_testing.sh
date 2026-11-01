#!/usr/bin/env bash
# Clears tower-defense progress and sessions for all characters.
# Usage: ./reset_tower_defense_for_testing.sh [db_url]
set -euo pipefail
DB_URL="${1:-${RAVENEST_GAME_DB_URL:-}}"
if [ -z "$DB_URL" ]; then
  echo "Error: set RAVENEST_GAME_DB_URL or pass the database URL" >&2
  exit 1
fi
psql "$DB_URL" -v ON_ERROR_STOP=1 <<'SQL'
DELETE FROM mini_game_progress WHERE mini_game = 'tower_defense';
DELETE FROM game_sessions WHERE mini_game = 'tower_defense';
DELETE FROM td_player_unlocks;
SQL
echo "Tower-defense progress cleared."
