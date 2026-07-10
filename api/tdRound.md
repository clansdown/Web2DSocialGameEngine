# `/api/tdRound`

Manages tower defense round lifecycle — creates a new game session or completes one.

**Authentication:** Required

## Request — Kickoff (no session_id)

```json
{
    "character_id": 1,
    "mini_game": "tower_defense",
    "level_id": 1
}
```

## Response — Kickoff

```json
{
    "status": "ok",
    "data": {
        "session_id": 1,
        "character_id": 1,
        "mini_game": "tower_defense",
        "level_id": 1,
        "difficulty": 1,
        "round_number": 0,
        "total_rounds": 1,
        "lives": 20,
        "gold": 100,
        "spawn_schedule": [
            {
                "enemy_id": "dire_rat",
                "count": 6,
                "interval_ms": 1900,
                "initial_delay_ms": 1500
            },
            {
                "enemy_id": "wolf",
                "count": 3,
                "interval_ms": 1900,
                "initial_delay_ms": 3400
            }
        ],
        "map_metadata": { ... },
        "mobs": { ... },
        "towers": { ... },
        "units": { ... }
    }
}
```

## Request — Complete Round

```json
{
    "session_id": 1,
    "character_id": 1,
    "lives_lost": 2,
    "gold_earned": 15
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `session_id` | int | yes | Session ID from kickoff |
| `character_id` | int | yes | Must match the session's character |
| `lives_lost` | int | yes | Number of enemies that reached endpoints |
| `gold_earned` | int | yes | Gold earned from kills this round |

## Response — Complete Round (game over)

```json
{
    "status": "ok",
    "data": {
        "session_id": 1,
        "game_over": true,
        "won": true,
        "lives": 18,
        "gold": 115,
        "score": 195,
        "rewards": { "gold": 10, "grain": 5 },
        "completed": true,
        "new_best_score": 195,
        "times_played": 1,
        "all_levels_done": false,
        "base_unlocked": false,
        "game_phase": "initial_mission",
        "land_patent_earned": false,
        "duke_right_earned": false
    }
}
```

## Error Responses

| Error | Scenario |
|-------|----------|
| `"character_id required"` | Missing character_id |
| `"mini_game required for new session"` | No session_id and no mini_game |
| `"level_id required for new session"` | No session_id and no level_id |
| `"Session not found or already completed"` | Invalid session_id |
| `"Session does not belong to this character"` | character_id mismatch |
| `"Already in a mini-game: ..."` | Character already playing another mini-game |
| `"Failed to process round: ..."` | Internal error |
| `"Failed to start TD session: ..."` | Internal error |

## Notes

- This endpoint replaces the need for separate `startMiniGame`/`endMiniGame` calls for tower defense
- The spawn schedule is generated server-side based on difficulty from mini_games.json
- On game completion, the server automatically handles phase transitions and unlock milestones
- Map metadata is loaded from `config/tower_defense/maps/` if the level specifies a map
