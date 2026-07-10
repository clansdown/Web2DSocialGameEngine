#!/bin/bash
# Resets all tower defense progress for testing.

DB="${1:-game/game.db}"

if [ ! -f "$DB" ]; then
  echo "Error: $DB not found"
  echo "Usage: $0 [path/to/game.db]"
  exit 1
fi

echo "Resetting tower defense progress in $DB..."
sqlite3 "$DB" "DELETE FROM mini_game_progress WHERE mini_game = 'tower_defense';"
sqlite3 "$DB" "DELETE FROM game_sessions WHERE mini_game = 'tower_defense';"
echo "Done."
