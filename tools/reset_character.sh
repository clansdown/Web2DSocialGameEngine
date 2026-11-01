#!/usr/bin/env bash
# Reset a character to initial state for path re-selection.
# Clears archetype, sex, progress, sessions, unlocks, and any fiefdom/barony.
#
# Usage:
#   ./tools/reset_character.sh                    # list characters
#   ./tools/reset_character.sh <character_id>     # reset (RAVENEST_GAME_DB_URL)
#   ./tools/reset_character.sh <id> <db_url>      # custom PostgreSQL URL
set -euo pipefail

DB_URL="${2:-${RAVENEST_GAME_DB_URL:-}}"
CHARACTER_ID="${1:-}"

if ! command -v psql >/dev/null 2>&1; then
    echo "Error: psql is required (install postgresql-client)" >&2
    exit 1
fi
if [ -z "$DB_URL" ]; then
    echo "Error: set RAVENEST_GAME_DB_URL or pass the database URL" >&2
    exit 1
fi

if [ -z "$CHARACTER_ID" ]; then
    echo "Characters:"
    psql "$DB_URL" -v ON_ERROR_STOP=1 -c \
        "SELECT id, display_name, COALESCE(archetype, '(none)') AS archetype
         FROM characters ORDER BY id;"
    echo ""
    echo "Usage: $0 <character_id> [db_url]"
    exit 0
fi

CHAR_EXISTS=$(psql "$DB_URL" -v ON_ERROR_STOP=1 -At \
    -c "SELECT COUNT(*) FROM characters WHERE id = $CHARACTER_ID;")
if [ "$CHAR_EXISTS" = "0" ]; then
    echo "Character $CHARACTER_ID not found"
    exit 1
fi

echo "Resetting character $CHARACTER_ID..."
psql "$DB_URL" -v ON_ERROR_STOP=1 <<SQL
BEGIN;
UPDATE characters SET archetype = NULL, sex = NULL WHERE id = $CHARACTER_ID;

UPDATE player_game_state SET
    game_phase = 'initial_mission',
    base_unlocked = 0,
    current_mini_game = NULL,
    current_level_id = NULL,
    entered_at = EXTRACT(EPOCH FROM NOW())::bigint,
    last_updated = EXTRACT(EPOCH FROM NOW())::bigint
WHERE character_id = $CHARACTER_ID;

DELETE FROM mini_game_progress WHERE character_id = $CHARACTER_ID;
DELETE FROM game_sessions WHERE character_id = $CHARACTER_ID;
DELETE FROM weeding_sessions WHERE character_id = $CHARACTER_ID;
DELETE FROM td_player_unlocks WHERE character_id = $CHARACTER_ID;
DELETE FROM retinue_members WHERE character_id = $CHARACTER_ID;
DELETE FROM fiefdom_armory WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdom_storage WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdom_walls WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM stationed_combatants WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdom_heroes WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM officials WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdom_buildings WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdom_river WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdoms WHERE owner_id = $CHARACTER_ID;
DELETE FROM barony_members WHERE character_id = $CHARACTER_ID;
DELETE FROM baronies WHERE owner_character_id = $CHARACTER_ID;
COMMIT;
SQL

echo "Character $CHARACTER_ID reset complete."
echo "Refresh the browser to choose a new path."
