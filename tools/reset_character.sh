#!/bin/bash
# Reset a character to initial state for path re-selection.
# Clears archetype, sex, all progress, sessions, unlocks, and any fiefdom/dukedom.
#
# Usage:
#   ./tools/reset_character.sh              # list characters
#   ./tools/reset_character.sh <character_id>  # reset specific character
#   ./tools/reset_character.sh <id> <db_path>  # custom database path

set -e

DB_PATH="${2:-game/game.db}"
CHARACTER_ID="$1"

if [ -z "$CHARACTER_ID" ]; then
    if [ ! -f "$DB_PATH" ]; then
        echo "Database not found at $DB_PATH"
        echo "Usage: $0 <character_id> [game_db_path]"
        exit 1
    fi
    echo "Characters in $DB_PATH:"
    sqlite3 "$DB_PATH" "SELECT id, display_name, COALESCE(archetype, '(none)') AS archetype FROM characters;" -header
    echo ""
    echo "Usage: $0 <character_id> [game_db_path]"
    exit 0
fi

if [ ! -f "$DB_PATH" ]; then
    echo "Database not found at $DB_PATH"
    exit 1
fi

# Verify character exists
CHAR_EXISTS=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM characters WHERE id = $CHARACTER_ID;")
if [ "$CHAR_EXISTS" = "0" ]; then
    echo "Character $CHARACTER_ID not found in $DB_PATH"
    exit 1
fi

echo "Resetting character $CHARACTER_ID in $DB_PATH..."

sqlite3 "$DB_PATH" <<SQL
-- Reset archetype and sex so PathSelect and SexSelect re-appear
UPDATE characters SET archetype = NULL, sex = NULL WHERE id = $CHARACTER_ID;

-- Reset player_game_state to fresh initial_mission
UPDATE player_game_state SET
    game_phase = 'initial_mission',
    base_unlocked = 0,
    current_mini_game = NULL,
    current_level_id = NULL,
    entered_at = CAST(strftime('%s', 'now') AS INTEGER),
    last_updated = CAST(strftime('%s', 'now') AS INTEGER)
WHERE character_id = $CHARACTER_ID;

-- Wipe all progress
DELETE FROM mini_game_progress WHERE character_id = $CHARACTER_ID;

-- Wipe game sessions
DELETE FROM game_sessions WHERE character_id = $CHARACTER_ID;
DELETE FROM weeding_sessions WHERE character_id = $CHARACTER_ID;

-- Wipe tower defense unlocks
DELETE FROM td_player_unlocks WHERE character_id = $CHARACTER_ID;

-- Wipe fiefdom if any (cascade through child tables)
DELETE FROM fiefdom_walls WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM stationed_combatants WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdom_heroes WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM officials WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdom_buildings WHERE fiefdom_id IN (SELECT id FROM fiefdoms WHERE owner_id = $CHARACTER_ID);
DELETE FROM fiefdoms WHERE owner_id = $CHARACTER_ID;

-- Remove from dukedom (both as member and as owner)
DELETE FROM dukedom_members WHERE character_id = $CHARACTER_ID;
DELETE FROM dukedoms WHERE owner_character_id = $CHARACTER_ID;
SQL

echo "Character $CHARACTER_ID reset complete."
echo "Refresh the browser to choose a new path."
