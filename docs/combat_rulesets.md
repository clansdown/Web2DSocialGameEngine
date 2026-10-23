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
| `death_handling` | string | `permanent` | `permanent` (lost forever), `wounded` (no-death: members drop to low health and recover), `respawn_after_seconds`, or `revive_resource` (revive via a future resource — treated as permanent until implemented) |
| `respawn_delay_seconds` | int | 0 | Respawn delay when `death_handling` is `respawn_after_seconds` |
| `revive_resource` | string \| null | null | Resource that powers revive-based rulesets (unimplemented) |
| `unit_caps` | `{per_player, total}` | — | Army size limits |
| `match_duration_seconds` | int | 1800 | **Safety cap** (annihilation has no expected match timer). Reaching it without the warband wiped = **draw** (see rts_design.md §3.4) |
| `win_conditions` | string[] | — | Annihilation PvE = `warband_wiped`. Human-readable list for PvP; engine implements team-wipe + most-survivors there |
| `fog_of_war` | bool | false | Reserved — per-player visibility lands later |
| `chat_enabled` | bool | true | Team chat on/off |
| `voice_enabled` | bool | true | WebRTC voice on/off |

## Rulesets

### `skirmish` (PvE)

- Up to 8 players on one team against the environment. **Annihilation**: the
  battle is won when the warband is wiped out; the player side loses when it is
  wiped. There is **no expected match timer** — `match_duration_seconds` (1800)
  is a **safety cap**: reaching it without the warband wiped is a **draw**
  (base XP only, **no** victory multiplier, and **neither** `last_victory_ts`
  nor `last_defeat_ts` is stamped).
- **Wounded (no-death)**: units lost in battle are **wounded** — their
  `retinue_members.health` drops to each unit's end-of-combat HP% (recovery
  clock restarts) and they recover in the manor/infirmary; they never die. The
  wound only applies when the end HP% is actually lower than the member's
  current health (an already-worse injury keeps its recovery clock). Below
  `retinue.json recovery.min_deploy_hp` (default 50%) they cannot deploy until
  healed past it.
- **Forfeit**: a leaver's units **withdraw unwounded**; their structures persist
  as team-owned; garrisoned archers eject unwounded. The leaver records a
  **defeat** (`last_defeat_ts`).

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

## Warbands & Enemy AI

Missions draw their enemy composition from `combat/warbands.json`
opportunities (rts_design.md §3.1/§11). Each warband entry carries a `group`
label plus optional per-group behavior:

- `tactic` — `charge` (close on the player), `hold` (stay in place), `guard`
  (hold the camp, engage approaches), `skirmish` (keep range), `reinforce`
  (rush to the alarm).
- `aggro_model` — `local` (only this group reacts), `call_to_arms` (nearby
  groups raise on contact), `global` (the whole warband knows).

Behavior *within* a group is driven by the class's fight style in
`combat/profiles.json` (goblin swarm/surround, bandit skirmisher,
orc stout/retreat-at-low-HP, troll juggernaut, dark-mage backline,
flask-lobber artillery) — see `server/docs/combat_system.md`.
