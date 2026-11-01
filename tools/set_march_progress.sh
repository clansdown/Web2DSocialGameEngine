#!/usr/bin/env bash
# Set a character's march progress (tower_defense wolf marche / weeding
# wildlands marche) to an exact point for testing campaign/phase flows.
#
# Progress lives in the PostgreSQL `mini_game_progress` table: one row per
# (character_id, mini_game, level_id). The track is determined by the
# character's current game_phase in player_game_state:
#   - initial_mission  -> starting track, levels 1-9
#   - baron_track      -> baron track, levels 10-25
#
# Usage:
#   ./tools/set_march_progress.sh                          # list characters
#   ./tools/set_march_progress.sh <target> <mini_game> <next_level> [db_url]
#
# <target>     numeric character id, username (exact), or display/safe name (substring).
# <mini_game>  tower_defense|wolf_marche|wolf  OR  weeding|wildlands_marche|assarter
# <next_level> the next level the player will play, relative to the current marche.
#
# The database URL defaults to RAVENEST_GAME_DB_URL.
set -euo pipefail

DB_URL="${4:-${RAVENEST_GAME_DB_URL:-}}"
TARGET="${1:-}"
MINI_GAME_ARG="${2:-}"
NEXT_LEVEL_ARG="${3:-}"

if ! command -v psql >/dev/null 2>&1; then
    echo "Error: psql is required (install postgresql-client)" >&2
    exit 1
fi
if [ -z "$DB_URL" ]; then
    echo "Error: set RAVENEST_GAME_DB_URL or pass the database URL as the fourth argument" >&2
    exit 1
fi

case "$MINI_GAME_ARG" in
    tower_defense|wolf|wolf_marche|td) MINI_GAME="tower_defense" ;;
    weeding|wildlands|wildlands_marche|assarter|wd) MINI_GAME="weeding" ;;
    "") MINI_GAME="" ;;
    *) echo "Error: unknown mini_game '$MINI_GAME_ARG'"
       echo "Use tower_defense|wolf_marche|wolf  or  weeding|wildlands_marche|assarter"
       exit 1 ;;
esac

psql_scalar() {
    psql "$DB_URL" -v ON_ERROR_STOP=1 -At -c "$1"
}

psql_table() {
    psql "$DB_URL" -v ON_ERROR_STOP=1 -c "$1"
}

LIST_SELECT="SELECT c.id AS id, c.display_name,
       COALESCE(u.username, '(no-user)') AS username,
       COALESCE(c.archetype, '(none)') AS archetype,
       COALESCE(p.game_phase, '(none)') AS game_phase
FROM characters c
LEFT JOIN users u ON u.id = c.user_id
LEFT JOIN player_game_state p ON p.character_id = c.id"

if [ -z "$TARGET" ]; then
    echo "Characters:"
    psql_table "$LIST_SELECT ORDER BY c.id;"
    echo ""
    echo "Usage: $0 <target> <mini_game> <next_level> [db_url]"
    exit 0
fi

if [ -z "$MINI_GAME" ]; then
    echo "Error: mini_game required" >&2
    exit 1
fi
if [ -z "$NEXT_LEVEL_ARG" ]; then
    echo "Error: next_level required" >&2
    exit 1
fi
if ! [[ "$NEXT_LEVEL_ARG" =~ ^[0-9]+$ ]]; then
    echo "Error: next_level must be an integer, got '$NEXT_LEVEL_ARG'" >&2
    exit 1
fi
NEXT_LEVEL=$((NEXT_LEVEL_ARG))

if [[ "$TARGET" =~ ^[0-9]+$ ]]; then
    COUNT=$(psql_scalar "SELECT COUNT(*) FROM characters WHERE id = $TARGET;")
    if [ "$COUNT" = "0" ]; then
        echo "Character $TARGET not found"
        exit 1
    fi
    CHARACTER_ID="$TARGET"
else
    MATCH_ID=$(psql "$DB_URL" -v ON_ERROR_STOP=1 -At -v target="$TARGET" \
        -c "SELECT c.id FROM characters c JOIN users u ON u.id = c.user_id
            WHERE lower(u.username) = lower(:'target') ORDER BY c.id LIMIT 2;")
    MATCH_COUNT=$(printf '%s\n' "$MATCH_ID" | grep -c '^[0-9]' || true)

    if [ "$MATCH_COUNT" = "1" ]; then
        CHARACTER_ID="$MATCH_ID"
    elif [ "$MATCH_COUNT" -gt "1" ]; then
        echo "Username '$TARGET' matches multiple characters:"
        psql "$DB_URL" -v ON_ERROR_STOP=1 -v target="$TARGET" -c \
            "$LIST_SELECT WHERE lower(u.username) = lower(:'target') ORDER BY c.id;"
        exit 1
    else
        NAME_MATCH=$(psql "$DB_URL" -v ON_ERROR_STOP=1 -At -v target="$TARGET" \
            -c "SELECT c.id FROM characters c
                WHERE c.display_name ILIKE '%' || :'target' || '%'
                   OR c.safe_display_name ILIKE '%' || :'target' || '%'
                ORDER BY c.id;")
        NAME_COUNT=$(printf '%s\n' "$NAME_MATCH" | grep -c '^[0-9]' || true)

        if [ "$NAME_COUNT" = "0" ]; then
            echo "No character matches '$TARGET'."
            psql_table "$LIST_SELECT ORDER BY c.id;"
            exit 1
        elif [ "$NAME_COUNT" -gt "1" ]; then
            echo "'$TARGET' matches multiple characters:"
            psql "$DB_URL" -v ON_ERROR_STOP=1 -v target="$TARGET" -c \
                "$LIST_SELECT WHERE c.display_name ILIKE '%' || :'target' || '%'
                 OR c.safe_display_name ILIKE '%' || :'target' || '%' ORDER BY c.id;"
            exit 1
        fi
        CHARACTER_ID="$NAME_MATCH"
    fi
fi

PHASE=$(psql_scalar "SELECT game_phase FROM player_game_state WHERE character_id = $CHARACTER_ID;")
if [ -z "$PHASE" ]; then
    echo "Error: character $CHARACTER_ID has no player_game_state row" >&2
    exit 1
fi

case "$PHASE" in
    initial_mission) MAX_NEXT=10 ;;
    baron_track) MAX_NEXT=17 ;;
    *)
        echo "Error: character $CHARACTER_ID is in phase '$PHASE' — marches are not active." >&2
        exit 1
        ;;
esac

if [ "$NEXT_LEVEL" -lt 1 ] || [ "$NEXT_LEVEL" -gt "$MAX_NEXT" ]; then
    echo "Error: next_level $NEXT_LEVEL is out of range for phase '$PHASE' (1..$MAX_NEXT)." >&2
    exit 1
fi

DISPLAY_NAME=$(psql_scalar "SELECT display_name FROM characters WHERE id = $CHARACTER_ID;")
FILL_MAX=$((NEXT_LEVEL - 1))
TRACK_LABEL="regular marche"
if [ "$PHASE" = "baron_track" ]; then
    FILL_MAX=$((NEXT_LEVEL + 8))
    TRACK_LABEL="barony marche"
fi

echo "Setting $MINI_GAME march progress for character $CHARACTER_ID ($DISPLAY_NAME, phase: $PHASE)..."
echo "  Next $TRACK_LABEL level to play: $NEXT_LEVEL"
if [ "$PHASE" = "baron_track" ]; then
    echo "  Regular marche levels 1-9 also marked completed"
fi

NOW=$(date +%s)
{
    echo "BEGIN;"
    echo "DELETE FROM mini_game_progress WHERE character_id = $CHARACTER_ID AND mini_game = '$MINI_GAME';"
    for ((i = 1; i <= FILL_MAX; i++)); do
        echo "INSERT INTO mini_game_progress (character_id, mini_game, level_id, completed, best_score, times_played, last_played) VALUES ($CHARACTER_ID, '$MINI_GAME', $i, 1, 0, 1, $NOW) ON CONFLICT (character_id, mini_game, level_id) DO UPDATE SET completed = 1, times_played = 1, last_played = $NOW;"
    done
    echo "COMMIT;"
} | psql "$DB_URL" -v ON_ERROR_STOP=1 >/dev/null

echo ""
echo "Resulting progress for $MINI_GAME:"
psql_table "SELECT level_id, completed, best_score, times_played
            FROM mini_game_progress
            WHERE character_id = $CHARACTER_ID AND mini_game = '$MINI_GAME'
            ORDER BY level_id;"
echo ""
echo "Game phase: $PHASE"
echo "Done."
