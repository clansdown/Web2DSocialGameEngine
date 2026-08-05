#!/bin/bash
# Set a character's march progress (tower_defense wolf marche / weeding wildlands marche)
# to an exact point, for testing campaign and phase flows.
#
# Progress lives in the mini_game_progress table: one row per
# (character_id, mini_game, level_id). The track is determined by the
# character's current game_phase in player_game_state:
#   - initial_mission  -> starting track, levels 1-9
#   - baron_track      -> baron track, levels 10-25
#
# Usage:
#   ./tools/set_march_progress.sh                          # list characters (find id)
#   ./tools/set_march_progress.sh <target> <mini_game> <next_level> [game_db_path]
#
# <target>     numeric character id, username (exact), or display/safe name (substring).
#              If multiple characters match, matching rows are listed and no change is made.
# <mini_game>  tower_defense|wolf_marche|wolf  OR  weeding|wildlands_marche|assarter
# <next_level> the next level the player will play, relative to the current marche;
#              levels 1..next_level-1 of that marche are marked completed. Ranges are
#              validated against the character's current phase:
#                initial_mission -> 1..10  (levels 1-9; 10 = all 9 done; server auto-advances to land_patent)
#                baron_track     -> 1..17  (levels 1-16 of the barony marche = absolute 10-25;
#                                           the 9 regular levels are always filled too; 17 = all done)
#
# Nothing else is modified: no game_phase, current_mini_game/current_level_id,
# sessions, or unlocks are touched.

set -e

DB_PATH="${4:-game/game.db}"
TARGET="$1"
MINI_GAME_ARG="$2"
NEXT_LEVEL_ARG="$3"

# Normalize mini_game argument
case "$MINI_GAME_ARG" in
    tower_defense|wolf|wolf_marche|td) MINI_GAME="tower_defense" ;;
    weeding|wildlands|wildlands_marche|assarter|wd) MINI_GAME="weeding" ;;
    "") MINI_GAME="" ;;
    *) echo "Error: unknown mini_game '$MINI_GAME_ARG'"
       echo "Use tower_defense|wolf_marche|wolf  or  weeding|wildlands_marche|assarter"
       exit 1 ;;
esac

if [ ! -f "$DB_PATH" ]; then
    echo "Database not found at $DB_PATH"
    echo "Usage: $0 <target> <mini_game> <next_level> [game_db_path]"
    exit 1
fi

LIST_COLUMNS="c.id AS id, c.display_name, COALESCE(u.username, '(no-user)') AS username, COALESCE(c.archetype, '(none)') AS archetype, COALESCE(p.game_phase, '(none)') AS game_phase"

list_characters() {
    sqlite3 -header -column "$DB_PATH" \
        "SELECT $LIST_COLUMNS
         FROM characters c
         LEFT JOIN users u ON u.id = c.user_id
         LEFT JOIN player_game_state p ON p.character_id = c.id
         ORDER BY c.id;"
}

show_matches() {
    local where="$1"
    sqlite3 -header -column "$DB_PATH" \
        "SELECT $LIST_COLUMNS
         FROM characters c
         LEFT JOIN users u ON u.id = c.user_id
         LEFT JOIN player_game_state p ON p.character_id = c.id
         WHERE $where
         ORDER BY c.id;"
}

if [ -z "$TARGET" ]; then
    echo "Characters in $DB_PATH:"
    list_characters
    echo ""
    echo "Usage: $0 <target> <mini_game> <next_level> [game_db_path]"
    echo "  <target> = character id, username, or display name"
    exit 0
fi

if [ -z "$MINI_GAME" ]; then
    echo "Error: mini_game required"
    echo "Use tower_defense|wolf_marche|wolf  or  weeding|wildlands_marche|assarter"
    exit 1
fi

if [ -z "$NEXT_LEVEL_ARG" ]; then
    echo "Error: next_level required"
    echo "Usage: $0 <target> <mini_game> <next_level> [game_db_path]"
    exit 1
fi

if ! [[ "$NEXT_LEVEL_ARG" =~ ^[0-9]+$ ]]; then
    echo "Error: next_level must be an integer, got '$NEXT_LEVEL_ARG'"
    exit 1
fi
NEXT_LEVEL=$((NEXT_LEVEL_ARG))

# Resolve target to a single character id.
if [[ "$TARGET" =~ ^[0-9]+$ ]]; then
    COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM characters WHERE id = $TARGET;")
    if [ "$COUNT" = "0" ]; then
        echo "Character $TARGET not found in $DB_PATH"
        exit 1
    fi
    CHARACTER_ID="$TARGET"
else
    # Exact username match first.
    MATCH_ID=$(sqlite3 "$DB_PATH" \
        "SELECT c.id FROM characters c JOIN users u ON u.id = c.user_id
         WHERE u.username = '$TARGET' ORDER BY c.id LIMIT 2;")
    MATCH_COUNT=$(echo -n "$MATCH_ID" | grep -c '^[0-9]' || true)

    if [ "$MATCH_COUNT" = "1" ]; then
        CHARACTER_ID=$(echo "$MATCH_ID" | head -1)
    elif [ "$MATCH_COUNT" -gt "1" ]; then
        echo "Username '$TARGET' matches multiple characters:"
        show_matches "u.username = '$TARGET'"
        echo "Use a specific character id instead."
        exit 1
    else
        # Fall back to display/safe name substring match.
        NAME_MATCH=$(sqlite3 "$DB_PATH" \
            "SELECT c.id FROM characters c
             WHERE c.display_name LIKE '%$TARGET%' OR c.safe_display_name LIKE '%$TARGET%'
             ORDER BY c.id;")
        NAME_COUNT=$(echo -n "$NAME_MATCH" | grep -c '^[0-9]' || true)

        if [ "$NAME_COUNT" = "0" ]; then
            echo "No character matches '$TARGET' (searched username, display name, safe name)."
            echo "Characters in $DB_PATH:"
            list_characters
            exit 1
        elif [ "$NAME_COUNT" -gt "1" ]; then
            echo "'$TARGET' matches multiple characters:"
            show_matches "c.display_name LIKE '%$TARGET%' OR c.safe_display_name LIKE '%$TARGET%'"
            echo "Use a specific character id instead."
            exit 1
        else
            CHARACTER_ID=$(echo "$NAME_MATCH" | head -1)
        fi
    fi
fi

# Determine current phase and validate next_level range for that track.
PHASE=$(sqlite3 "$DB_PATH" \
    "SELECT game_phase FROM player_game_state WHERE character_id = $CHARACTER_ID;")
if [ -z "$PHASE" ]; then
    echo "Error: character $CHARACTER_ID has no player_game_state row"
    exit 1
fi

case "$PHASE" in
    initial_mission)
        MAX_NEXT=10
        MIN_NEXT=1
        ;;
    baron_track)
        MAX_NEXT=17
        MIN_NEXT=1
        ;;
    *)
        echo "Error: character $CHARACTER_ID is in phase '$PHASE' — marches are not the active campaign."
        echo "The march tracks run during 'initial_mission' or 'baron_track' phases only."
        exit 1
        ;;
esac

if [ "$NEXT_LEVEL" -lt "$MIN_NEXT" ] || [ "$NEXT_LEVEL" -gt "$MAX_NEXT" ]; then
    echo "Error: next_level $NEXT_LEVEL is out of range for phase '$PHASE' (expected $MIN_NEXT..$MAX_NEXT)."
    exit 1
fi

DISPLAY_NAME=$(sqlite3 "$DB_PATH" \
    "SELECT display_name FROM characters WHERE id = $CHARACTER_ID;")

# The argument is relative to the active marche. Convert to the absolute
# level range to fill: initial_mission levels 1..NEXT_LEVEL-1 (relative ==
# absolute), baron_track regular 1-9 always + barony 1..NEXT_LEVEL-1
# (absolute 1..NEXT_LEVEL+8).
FILL_MAX=$((NEXT_LEVEL - 1))
TRACK_LABEL="regular marche"
if [ "$PHASE" = "baron_track" ]; then
    FILL_MAX=$((NEXT_LEVEL + 8))
    TRACK_LABEL="barony marche"
fi

echo "Setting $MINI_GAME march progress for character $CHARACTER_ID ($DISPLAY_NAME, phase: $PHASE)..."
echo "  Next $TRACK_LABEL level to play: $NEXT_LEVEL (levels 1..$((NEXT_LEVEL - 1)) of the $TRACK_LABEL marked completed)"
if [ "$PHASE" = "baron_track" ]; then
    echo "  Regular marche levels 1-9 also marked completed (idempotent)"
fi

NOW=$(date +%s)

# Reset progress for this (character_id, mini_game) pair, then mark 1..FILL_MAX completed.
{
    echo "BEGIN;"
    echo "DELETE FROM mini_game_progress WHERE character_id = $CHARACTER_ID AND mini_game = '$MINI_GAME';"
    for ((i = 1; i <= FILL_MAX; i++)); do
        echo "INSERT INTO mini_game_progress (character_id, mini_game, level_id, completed, best_score, times_played, last_played) VALUES ($CHARACTER_ID, '$MINI_GAME', $i, 1, 0, 1, $NOW);"
    done
    echo "COMMIT;"
} | sqlite3 "$DB_PATH"

echo ""
echo "Resulting progress for $MINI_GAME:"
sqlite3 -header -column "$DB_PATH" \
    "SELECT level_id, completed, best_score, times_played
     FROM mini_game_progress
     WHERE character_id = $CHARACTER_ID AND mini_game = '$MINI_GAME'
     ORDER BY level_id;"
echo ""
echo "Game phase: $PHASE"
echo "Done."
