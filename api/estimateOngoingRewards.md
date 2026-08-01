# `/api/estimateOngoingRewards`

Estimates the silver reward for an ongoing-mode mini-game at the given
difficulty and size, adjusted for the character's current reward pool
(diminishing returns). This is a **read-only hint for display** — the server
recomputes and enforces the actual payout when the game completes.

**Authentication:** Required

## Request

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "character_id": 1,
  "mini_game": "tower_defense",
  "difficulty": 2,
  "size": 8
}
```

- `mini_game`: `"tower_defense"` or `"weeding"`
- `difficulty`: one of the game's configured `difficulty_options` (default: config default)
- `size`: rounds (tower_defense) or grid size (weeding), one of `size_options` (default: config default)

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "silver_pence": 10,
    "silver_formatted": "1s 4d",
    "base_silver_pence": 12,
    "reward_multiplier": 0.5,
    "pool": { "full": 0, "half": 4 }
  },
  "token": "new-token-string"
}
```

- `silver_pence`: expected reward in silver pence (pool-adjusted)
- `silver_formatted`: same amount formatted in old-English (e.g. `"2s 6d"`, `"£1 3s 4d"`)
- `base_silver_pence`: reward before diminishing returns
- `reward_multiplier`: effective multiplier (1.0 full, 0.5 half, 0.25 quarter)
- `pool`: current effective full/half reward pool counts for the character

### Error

```json
{ "status": "ok", "error": "No ongoing config for mini_game: unknown" }
```

### Notes

- This endpoint never mutates the reward pool; use it freely as settings change.
- Reward formula: `base_pence = size_reward_pence + difficulty_coeff_pence × (difficulty − 1)`.
- Diminishing returns (server-enforced, shared across ongoing mini-games):
  - Full pool (max 15, replenished 5/day): full reward.
  - Half pool (max 5, replenished 5/day): half reward, rounded down, min 1 penny.
  - Otherwise: quarter reward, rounded down, min 1 penny.
