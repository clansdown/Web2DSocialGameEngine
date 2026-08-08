# Combat Rulesets

`game/config/combat/rulesets.json` defines the mission rules for the realtime
combat game. Every match is created with exactly one ruleset; the same engine
supports PvE skirmishes and PvP scrimmages by switching rules, not code.

The file is an object:

```json
{
    "format_version": "1.0",
    "rulesets": [ ... ]
}
```

## Ruleset Fields

| Field | Type | Default | Description |
|---|---|---|---|
| `id` | string | — | Unique lowercase snake_case id (referenced by `/api/combatCreate`) |
| `name` | string | — | Display name |
| `mode` | string | — | `pve` or `pvp` |
| `players` | `{min, max}` | — | Lobby size bounds (PvE 1–8 default; PvP up to 64) |
| `teams` | `{min, max}` | — | Team count bounds (informational for now; auto-balancing later) |
| `death_handling` | string | `permanent` | `permanent` (lost forever), `respawn_after_seconds`, or `revive_resource` (revive via a future resource — treated as permanent until implemented) |
| `respawn_delay_seconds` | int | 0 | Respawn delay when `death_handling` is `respawn_after_seconds` |
| `revive_resource` | string \| null | null | Resource that powers revive-based rulesets (unimplemented) |
| `unit_caps` | `{per_player, total}` | — | Army size limits |
| `match_duration_seconds` | int | 1800 | Battle length; survival decides at the end |
| `win_conditions` | string[] | — | Human-readable list (informational; engine implements team-wipe + survival) |
| `fog_of_war` | bool | false | Reserved — per-player visibility lands later |
| `chat_enabled` | bool | true | Team chat on/off |
| `voice_enabled` | bool | true | WebRTC voice on/off |

## Rulesets

### `skirmish` (PvE)

- Up to 8 players on one team against the environment (enemy AI is a later
  milestone — currently the battle runs until the timer, and survival is victory).
- **Permanent death**: units lost in battle become `casualty` in
  `retinue_members` when the match ends.

### `scrimmage` (PvP, engine-ready)

- 2–64 players on two auto-balanced teams (alternating).
- **Respawn**: units return to their team's spawn point 20 seconds after death.
- Fog of war enabled (once visibility is implemented).

## Flexibility

- Unknown fields are **warnings** in the config linter, not errors — the
  format is meant to grow (respawn budgets, deployment zones, etc.) as the
  game gains experience.
- Configs are hot-reloadable (mtime-polled); a ruleset edit applies to the
  next created match without a server restart.
