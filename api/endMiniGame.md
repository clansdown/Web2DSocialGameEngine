# `/api/endMiniGame`

Ends an active mini-game session, records the outcome, and processes rewards.

**Authentication:** Required

## Request

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "character_id": 1,
  "mini_game": "tower_defense",
  "level_id": 3,
  "won": true,
  "score": 85
}
```

## Response

### Success (200 OK) — Normal completion

```json
{
  "status": "ok",
  "data": {
    "completed": true,
    "score": 85,
    "new_best_score": 85,
    "times_played": 1,
    "all_levels_done": false,
    "base_unlocked": false,
    "game_phase": "initial_mission",
    "next_level_id": 4,
    "rewards": { "gold": 15, "grain": 8 }
  },
  "token": "new-token-string"
}
```

### Success (200 OK) — Campaign complete, base unlocked

```json
{
  "status": "ok",
  "data": {
    "completed": true,
    "score": 95,
    "new_best_score": 95,
    "times_played": 3,
    "all_levels_done": true,
    "base_unlocked": true,
    "game_phase": "sandbox",
    "next_level_id": null,
    "rewards": { "gold": 40, "grain": 20 },
    "completion_bonus": { "gold": 100, "wood": 50, "grain": 50, "stone": 30 }
  },
  "token": "new-token-string"
}
```

### Success (200 OK) — Defeat

```json
{
  "status": "ok",
  "data": {
    "completed": false,
    "score": 0,
    "new_best_score": 0,
    "times_played": 2,
    "all_levels_done": false,
    "base_unlocked": false,
    "game_phase": "initial_mission",
    "next_level_id": 3,
    "rewards": {}
  },
  "token": "new-token-string"
}
```

### Errors

```json
{ "status": "ok", "error": "Not currently playing tower_defense" }
```

## Fields

| Field | Type | Description |
|-------|------|-------------|
| `completed` | boolean | Whether the player won the level |
| `score` | int | The player's score for this attempt |
| `new_best_score` | int | Updated best score for this level |
| `times_played` | int | Total attempts on this level |
| `all_levels_done` | boolean | True if all 9 levels in the campaign are completed |
| `base_unlocked` | boolean | True if completing this level unlocked the base |
| `game_phase` | string | Updated game phase (may transition to `"sandbox"`) |
| `next_level_id` | int or null | Next available level ID, if any |
| `rewards` | object | Resources earned from this level |
| `completion_bonus` | object or null | One-time bonus for completing all 9 levels |
