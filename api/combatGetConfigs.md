# `/api/combatGetConfigs`

Returns the available combat **rulesets**, **maps**, and **mission
opportunities** for the match-creation lobby. Reads from the hot-reloadable
configs (`combat/rulesets.json`, the `combat/maps/` directory, and
`combat/warbands.json`), so new content appears without a server restart.

**Requires authentication.**

## Request

```json
{
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "rulesets": [
            { "id": "skirmish", "name": "Skirmish", "mode": "pve" },
            { "id": "scrimmage", "name": "Scrimmage", "mode": "pvp" }
        ],
        "maps": [
            { "id": "meadow", "file": "meadow.json" }
        ],
        "opportunities": [
            {
                "id": "goblin_raiders",
                "name": "Goblin Raiders",
                "tier": 1,
                "ruleset": "skirmish",
                "map": "meadow",
                "xp_base": 60,
                "flavor": "A score of goblins has been raiding the sheep-walks…",
                "warband": [
                    { "unit_class": "goblin", "count": 20, "level": 1 },
                    { "unit_class": "bandit", "count": 4, "level": 1 }
                ],
                "total_units": 24
            }
        ],
        "structures": {
            "barricade": { "id": "barricade", "name": "Barricade", "footprint": 1, "max_hp": 150, "build_time_seconds": 8, "max_per_player": 8, "blocks_ranged": false }
        },
        "formations": {
            "line": { "id": "line", "name": "Line" }
        },
        "combatant_radius": {
            "spearman": 0.03, "knight": 0.035, "goblin": 0.025, "troll": 0.065
        },
        "combatant_art": {
            "spearman": {
                "walk": {
                    "frames": 8,
                    "fps": 10,
                    "views": {
                        "e": "e", "se": "se", "s": "s", "sw": "sw",
                        "w": "w", "nw": "nw", "n": "n", "ne": "ne"
                    }
                },
                "idle": {
                    "frames": 1,
                    "fps": 1,
                    "views": {
                        "e": "e", "se": "se", "s": "s", "sw": "sw",
                        "w": "w", "nw": "nw", "n": "n", "ne": "ne"
                    }
                }
            }
        }
    }
}
```

## Field Descriptions

- `opportunities[]` — the mission picker (rts_design.md §4.1). Each entry bundles
  a ruleset + map + warband composition + tier + danger flavor; the warband is
  **fixed at match creation** (D62). `xp_base` is the per-fielded-member base XP the
  opportunity pays (victory multiplies; draw = base only).
- `structures` — the temporary-structure catalog (rts_design.md §7.1) keyed by
  type id; powers the client's Construct palette (`CombatCommandBar`).
- `formations` — the formation-shape catalog (rts_design.md §8.3) keyed by id;
  powers the client's Formation palette.
- `combatant_radius` — per-class **body radius** in normalized board units (the
  required `size` field of `player_combatants.json`/`enemy_combatants.json`, keyed
  by class id). The client uses it to size deploy/roster tokens and the in-battle
  drawn sprite/token, **independent of the source image dimensions** — bigger units
  read bigger (a troll ≈ 2.6× a goblin). The same value rides each in-battle unit
  row as `radius`. See `server/docs/combat_system.md` → "RTS combat model".
- `combatant_art` — per-class **directional sprite clips** (rts_design.md D24/D71)
  keyed by class id. Each clip carries a server-discovered `frames` count (counted
  from the image files in each set directory — never declared in config), `fps`
  from config, and optional `views` (direction → set name or `{ set, mirror }`,
  where `mirror` is an optional horizontal flip across the vertical axis). `idle`
  is the stationary standing pose (usually a single frame). The server advertises
  **only** clips/sets with an authored frame-set directory under
  `game/images/combatants/<class>/<clip>/<set>/`, so the client never requests
  missing art; classes without art render procedurally. See
  `server/docs/combat_system.md` → "Combatant Art (RTS)".
- `maps[]` / `rulesets[]` — retained for legacy creates and for the client to
  cross-reference opportunity `ruleset`/`map` ids.

## Error Responses

```json
{ "error": "..." }
```

(Standard auth errors only.)