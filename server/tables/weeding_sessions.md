# weeding_sessions Table

## Purpose

Stores active weeding minigame sessions (Assarter of the Wildlands). Each row is one complete game session from kickoff to win/forfeit. The full game state (board, round, equipped tool, etc.) is stored as a JSON blob in `session_json`.

## Schema

```sql
CREATE TABLE weeding_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    level_id INTEGER NOT NULL,
    started_at INTEGER NOT NULL,
    last_activity INTEGER NOT NULL,
    state TEXT NOT NULL DEFAULT 'active',
    session_json TEXT NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `id` | INTEGER | Auto-increment primary key |
| `character_id` | INTEGER | FK to `characters(id)`. The player who owns this session |
| `level_id` | INTEGER | Which level is being played (1-9 campaign, 10-25 baron, 0 sandbox) |
| `started_at` | INTEGER | Unix timestamp when the session was created |
| `last_activity` | INTEGER | Unix timestamp of the last action/turn |
| `state` | TEXT | Session state: `'active'`, `'won'`, `'forfeited'` |
| `session_json` | TEXT | Full game state as a JSON string |

## Indexes

- `idx_weeding_sessions_character` on `character_id` — fast lookup of active sessions per character

## session_json Structure

The `session_json` blob contains the complete game state. Example:

```json
{
    "board": [[
        {"plant_type": null, "progress": 0, "actions_needed": 0, "is_smother_crop": false, "is_blocked": true, "is_accessible": false},
        {"plant_type": "bindweed", "progress": 0, "actions_needed": 1, "is_smother_crop": false, "is_blocked": false, "is_accessible": true}
    ]],
    "grid_size": 4,
    "round": 1,
    "actions_remaining": 2,
    "pending_switch": false,
    "equipped_tool": "sickle",
    "par": 12,
    "won": false,
    "score": 0,
    "initial_weed_count": 6,
    "map_id": "map_1"
}
```

## Relationships

- **`character_id` → `characters(id)`**: Each session belongs to exactly one character
- **No joins across databases**: `weeding_sessions` lives in `game.db` (same database as characters)

## Usage Notes

- At most one active weeding session per character (enforced by server logic)
- Session is marked `won` when all valid squares are smother crops
- Session is marked `forfeited` when the player gives up
- Completed sessions are NOT deleted — they persist for record-keeping
- Active sessions are checked when starting a new game (prevents concurrent sessions)
- `player_game_state.current_mini_game` is set to `'weeding'` during an active session
