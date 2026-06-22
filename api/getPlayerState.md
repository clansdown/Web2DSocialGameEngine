# `/api/getPlayerState`

Retrieves the current player's game state including phase, active mini-game, and level progress.

**Authentication:** Required

## Request

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "character_id": 1
}
```

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "character_id": 1,
    "game_phase": "initial_mission",
    "current_mini_game": null,
    "current_level_id": null,
    "base_unlocked": false,
    "entered_at": 1700000000,
    "last_updated": 1700000000,
    "progress": [
      {
        "id": 1,
        "character_id": 1,
        "mini_game": "tower_defense",
        "level_id": 1,
        "completed": true,
        "best_score": 85,
        "times_played": 2,
        "last_played": 1700000100
      },
      {
        "id": 2,
        "character_id": 1,
        "mini_game": "tower_defense",
        "level_id": 2,
        "completed": false,
        "best_score": 0,
        "times_played": 0,
        "last_played": 0
      }
    ]
  },
  "token": "new-token-string"
}
```

### Error

```json
{
  "status": "ok",
  "error": "character_id required"
}
```

## Fields

| Field | Type | Description |
|-------|------|-------------|
| `game_phase` | string | `"initial_mission"` before base unlock, `"sandbox"` after |
| `current_mini_game` | string or null | Active mini-game name if currently playing, else null |
| `current_level_id` | int or null | Active level ID if currently playing, else null |
| `base_unlocked` | boolean | Whether the player has earned the right to build a base |
| `progress` | array | Array of MiniGameProgress objects for all mini-games |

### MiniGameProgress Fields

| Field | Type | Description |
|-------|------|-------------|
| `mini_game` | string | Mini-game name (e.g. `"tower_defense"`) |
| `level_id` | int | Level number (1-9 for 3x3 grid) |
| `completed` | boolean | Whether this level has been won |
| `best_score` | int | Best score achieved on this level |
| `times_played` | int | Number of times this level has been attempted |
| `last_played` | int | Unix timestamp of last play attempt |
