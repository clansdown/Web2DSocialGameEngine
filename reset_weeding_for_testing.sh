#!/bin/bash
# Resets all weeding (assarter) progress for testing.

DB="${1:-game/game.db}"

if [ ! -f "$DB" ]; then
  echo "Error: $DB not found"
  echo "Usage: $0 [path/to/game.db]"
  exit 1
fi

echo "Resetting weeding progress in $DB..."
sqlite3 "$DB" "DELETE FROM mini_game_progress WHERE mini_game = 'weeding';"
sqlite3 "$DB" "DELETE FROM weeding_sessions;"
sqlite3 "$DB" "UPDATE player_game_state SET current_mini_game = NULL, current_level_id = NULL WHERE current_mini_game = 'weeding';"
echo "Done."
