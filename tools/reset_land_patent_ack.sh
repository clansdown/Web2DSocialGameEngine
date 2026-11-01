#!/usr/bin/env bash
# Clears land_patent_acknowledged for a character so the patent screen reappears.
# Usage: ./tools/reset_land_patent_ack.sh <character_id> [db_url]
set -euo pipefail

DB_URL="${2:-${RAVENEST_GAME_DB_URL:-}}"
CHARACTER_ID="${1:-}"

if [ -z "$DB_URL" ]; then
    echo "Error: set RAVENEST_GAME_DB_URL or pass the database URL" >&2
    exit 1
fi
if [ -z "$CHARACTER_ID" ]; then
    psql "$DB_URL" -v ON_ERROR_STOP=1 -c \
        "SELECT id, display_name, COALESCE(archetype, '(none)') AS archetype
         FROM characters ORDER BY id;"
    echo "Usage: $0 <character_id> [db_url]"
    exit 0
fi

COUNT=$(psql "$DB_URL" -v ON_ERROR_STOP=1 -At \
    -c "SELECT COUNT(*) FROM characters WHERE id = $CHARACTER_ID;")
if [ "$COUNT" = "0" ]; then
    echo "Character $CHARACTER_ID not found"
    exit 1
fi

psql "$DB_URL" -v ON_ERROR_STOP=1 \
    -c "UPDATE player_game_state SET land_patent_acknowledged = 0 WHERE character_id = $CHARACTER_ID;"
echo "Cleared land_patent_acknowledged for character $CHARACTER_ID."
