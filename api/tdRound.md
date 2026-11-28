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
        "cost_suffix": "c",
        "ui_theme": {
            "ink": { "...": "..." },
            "surfaces": { "...": "..." }
        },
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

### Starting gold

The `gold` value in the kickoff response is resolved per new session:

1. The matching `levels[]` or `baron_levels[]` entry's `starting_gold`, if present.
2. `tower_defense.starting_gold` from `mini_games.json`.
3. Fallback `100`.

`level_id: 0` (ongoing/custom games) uses the `tower_defense.starting_gold`
default. Existing active sessions keep the gold already stored on the session.

Current campaign values: Wolf Marche levels 1–5 start with `100`; levels 6–9,
ongoing games, and the baron challenge start with `120`.

### Cost suffix

`cost_suffix` comes from `tower_defense.cost_suffix` in `mini_games.json`
(currently `c`). The client renders each picker's level-1 cost as
`<cost><suffix>` in the button's top-right corner; the value is unchanged and
the suffix is display-only.

### UI theme

`ui_theme` is the Tower Defense-scoped HUD text theme from
`tower_defense/ui_theme.json`. It is included on both kickoff and resume
responses. The client resolves semantic ink roles for the copper readout, lives
readout, picker/context/start/forfeit/help labels, and the manually drawn cost
badge.

Effect dimensions are **canvas units**, not CSS pixels. Tower Defense renders
to a fixed virtual canvas and scales the whole canvas with CSS, so text effects
scale naturally and are not part of browser-resize relayout. The cost badge uses
the same `money` ink as the copper readout; the player-facing currency is copper,
not gold. Missing or malformed theme data falls back to a readable built-in
theme.

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
        "completion_bonus": { "gold": 0, "wood": 0, "grain": 0 },
        "completed": true,
        "new_best_score": 195,
        "times_played": 1,
        "all_levels_done": false,
        "base_unlocked": false,
        "game_phase": "initial_mission",
        "land_patent_earned": false,
        "baron_right_earned": false
    }
}
```

Campaign rewards resolve from the quest file first (`levels.<id>.reward`, then
`default_level_reward`), falling back to `mini_games.json`; the one-time
`completion_bonus` resolves from the quest file then `mini_games.json`. Rewards
are credited to the fiefdom only on first completion; replays return
`rewards: {}` and pay nothing. See `server/docs/mini_game_rewards.md`.

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
- The client creates the lives readout only when the level begins with more than one life. Adding lives to a level that started with none is unsupported.
- On game completion, the server automatically handles phase transitions and unlock milestones
- Map metadata is loaded from `config/tower_defense/maps/` if the level specifies a map
