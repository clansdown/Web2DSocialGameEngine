# `/api/getMiniGameConfig`

Returns mini-game configuration data. Can filter by specific mini-game or return all.

**Authentication:** Required

## Request

### All mini-games

```json
{
  "auth": { "username": "player1", "token": "existing-token" }
}
```

### Specific mini-game

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "mini_game": "tower_defense"
}
```

## Response

### Full response

```json
{
  "status": "ok",
  "data": {
    "tower_defense": {
      "name": "tower_defense",
      "display_name": "Tower Defense",
      "description": "Defend your lanes from waves of encroaching enemies",
      "grid_size": 3,
      "sequential": true,
      "levels": [
        { "id": 1, "row": 0, "col": 0, "difficulty": 1, "reward": { "gold": 10, "grain": 5 } },
        { "id": 2, "row": 0, "col": 1, "difficulty": 1, "reward": { "gold": 10, "grain": 5 } }
      ],
      "completion_bonus": {
        "base_unlock": true,
        "resources": { "gold": 100, "wood": 50, "grain": 50, "stone": 30 }
      },
      "replay_config": {
        "random_generation": true,
        "difficulty_scaling": { "base": 1 },
        "reward_scaling": { "base": 5 }
      },
      "ongoing": {
        "game": "tower_defense",
        "difficulty_options": [1, 2, 3],
        "default_difficulty": 1,
        "difficulty_coeff_pence": 1,
        "size_options": [
          { "value": 3, "reward_pence": 1 },
          { "value": 5, "reward_pence": 2 },
          { "value": 10, "reward_pence": 4 },
          { "value": 20, "reward_pence": 5 }
        ],
        "default_size": 5
      }
    },
    "weeding": {
      "name": "weeding",
      "display_name": "Weeding",
      "description": "Clear the land of weeds and debris to prepare for construction",
      "grid_size": 3,
      "sequential": true,
      "levels": [
        { "id": 1, "row": 0, "col": 0, "difficulty": 1, "reward": { "gold": 10, "wood": 5 } }
      ],
      "completion_bonus": {
        "base_unlock": true,
        "resources": { "gold": 100, "grain": 50, "wood": 50, "stone": 30 }
      },
      "replay_config": {
        "random_generation": true,
        "difficulty_scaling": { "base": 1 },
        "reward_scaling": { "base": 5 }
      },
      "ongoing": {
        "game": "weeding",
        "difficulty_options": [1, 2, 3],
        "default_difficulty": 1,
        "difficulty_coeff_pence": 1,
        "size_options": [
          { "value": 4, "reward_pence": 1 }
        ],
        "default_size": 4
      }
    }
  },
  "token": "new-token-string"
}
```

### Single mini-game

```json
{
  "status": "ok",
  "data": {
    "tower_defense": {
      "name": "tower_defense",
      "display_name": "Tower Defense",
      ...
    }
  },
  "token": "new-token-string"
}
```

### Error

```json
{ "status": "ok", "error": "Unknown mini_game: unknown_game" }
```

## Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Internal mini-game identifier |
| `display_name` | string | Human-readable name |
| `description` | string | Short description |
| `grid_size` | int | Grid dimension (e.g. 3 = 3x3 = 9 levels) |
| `sequential` | boolean | Whether levels must be completed in order |
| `levels` | array | Level definitions with id, row, col, difficulty, reward |
| `completion_bonus` | object | One-time rewards for completing all levels |
| `replay_config` | object | Configuration for random generation in sandbox mode |
| `ongoing` | object | Ongoing-mode options: `difficulty_options`, `default_difficulty`, `difficulty_coeff_pence`, `size_options` (array of `{value, reward_pence}`), `default_size`. `value` is rounds for tower_defense, grid size for weeding. Server-served so the client only offers what the server allows. |

## See Also

- This data is also available through `/api/getGameInfo` under the `configs.mini_games` key.
