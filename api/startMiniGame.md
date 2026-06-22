# `/api/startMiniGame`

Starts a mini-game session for the specified character. Validates prerequisites and returns level configuration.

**Authentication:** Required

## Request

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "character_id": 1,
  "mini_game": "tower_defense"
}
```

## Response

### Success (200 OK) — Initial mission mode

When `game_phase` is `"initial_mission"`, the server auto-selects the next incomplete level in order:

```json
{
  "status": "ok",
  "data": {
    "character_id": 1,
    "mini_game": "tower_defense",
    "level_id": 3,
    "id": 3,
    "row": 1,
    "col": 0,
    "difficulty": 2,
    "reward": { "gold": 15, "grain": 8 },
    "map": "tower_defense_level_3",
    "num_waves": 6,
    "lane_count": 1,
    "enemy_types": ["basic"]
  },
  "token": "new-token-string"
}
```

### Success (200 OK) — Sandbox mode

When `game_phase` is `"sandbox"`, the server generates a random level:

```json
{
  "status": "ok",
  "data": {
    "character_id": 1,
    "mini_game": "tower_defense",
    "level_id": 0,
    "map": "random",
    "difficulty": 1,
    "num_waves": 5,
    "lane_count": 1,
    "enemy_types": ["basic"],
    "rewards": { "gold": 5 }
  },
  "token": "new-token-string"
}
```

### Error Responses

```json
{ "status": "ok", "error": "Already in a mini-game: tower_defense" }
{ "status": "ok", "error": "All levels completed for tower_defense" }
{ "status": "ok", "error": "Previous level not completed" }
{ "status": "ok", "error": "Unknown mini_game: weeding" }
```

## Fields

| Field | Type | Description |
|-------|------|-------------|
| `character_id` | int | The character now playing |
| `mini_game` | string | The mini-game being played |
| `level_id` | int | Level ID (0 for random, 1-9 for initial mission) |
| `map` | string | Map identifier or `"random"` |
| `difficulty` | int | Difficulty rating (1-5) |
| `num_waves` | int | Number of waves in the level (TD-specific) |
| `lane_count` | int | Number of lanes (TD-specific) |
| `enemy_types` | string[] | Enemy type identifiers (TD-specific) |
| `rewards` | object | Rewards available upon completion |

## Errors

- `character_id required` — Missing character_id field
- `mini_game required` — Missing mini_game field
- `Already in a mini-game: ...` — Character is already in a session; end it first
- `All levels completed for ...` — All 9 levels done; base is unlocked
- `Previous level not completed` — Sequential levels must be played in order
- `Unknown mini_game: ...` — Mini-game not registered in config
