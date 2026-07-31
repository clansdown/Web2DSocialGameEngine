#!/bin/bash
# Reset the land_patent_acknowledged flag for a character so the land patent
# overlay reappears on their next player-state fetch. For testing.
#
# Usage:
#   ./tools/reset_land_patent_ack.sh               # list characters
#   ./tools/reset_land_patent_ack.sh <character_id>  # reset flag
#   ./tools/reset_land_patent_ack.sh <id> <db_path>  # custom database path

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

echo "Resetting land_patent_acknowledged for character $CHARACTER_ID in $DB_PATH..."

sqlite3 "$DB_PATH" "UPDATE player_game_state SET land_patent_acknowledged = 0 WHERE character_id = $CHARACTER_ID;"

echo "Done. The land patent overlay will reappear on the character's next player-state fetch."
