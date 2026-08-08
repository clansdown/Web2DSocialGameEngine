# Combat Maps

Map files live in `game/config/combat/maps/*.json`. They are loaded by
`server/combat/CombatMapCache.cpp` with a **lenient, forward-compatible
parse**: known camelCase fields are normalized to snake_case, and unknown
fields are preserved (warned about by the linter, never rejected) — so the
format can evolve as the game gains experience without breaking existing maps.

## Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `format_version` | string | yes | Format gate; newer versions load with a log line |
| `name` | string | yes | Map id (must match the filename stem) |
| `image_filename` | string | no | Background image in `game/images/combat/maps/` |
| `width_tiles` | int | yes | Grid width |
| `height_tiles` | int | yes | Grid height |
| `tile_size_px` | int | no | Pixels per tile (informational; rendering scale) |
| `tile_costs` | number[] | yes | Flat array, length `width x height`; `0` = impassable, `1` = open, `>1` = slower terrain |
| `spawn_points` | array | no | `{id, x, y, team}` with **normalized** coordinates (0..1, top-left origin) |
| `start_positions` | array | no | Reserved (per-player starts for PvP) |
| `obstacles` | array | no | `{id, x, y, radius}` circles in normalized coordinates (visual + collision) |

## Example

```json
{
    "format_version": "1.0",
    "name": "meadow",
    "image_filename": "meadow.png",
    "width_tiles": 16,
    "height_tiles": 16,
    "tile_size_px": 32,
    "tile_costs": [ 1,1,1, ... ],
    "spawn_points": [
        { "id": "sp_team1", "x": 0.125, "y": 0.5, "team": 1 },
        { "id": "sp_team2", "x": 0.875, "y": 0.5, "team": 2 }
    ],
    "obstacles": [
        { "id": "o1", "x": 0.375, "y": 0.5, "radius": 0.06 }
    ]
}
```

## Coordinates

- Normalized 0..1 with origin at top-left, matching the tower-defense map
  convention (`pixel = normalized * board_dimension`).
- `tile_costs` is the **pathfinding foundation**: future enemy AI (flow fields
  or A*) consumes it directly. Adding terrain types later is additive data
  (e.g. a `terrain_types` id → cost table), not a schema change.

## Runtime Behavior

- Maps are **hot-reloaded**: `CombatMapCache` rescans on directory mtime
  change (POSIX `stat()` — see AGENTS.md filesystem-caching note), so new or
  edited maps appear without a server restart.
- The full normalized map JSON is shipped to clients in the `welcome`
  message; clients read what they need and ignore the rest.
- The config linter (`tools/check_configs.py`) errors only on malformed
  required fields; **unknown fields are warnings**.
