# POST /api/getBuildingConfigs

Get building type configurations for the manor system. **Requires authentication.** No parameters other than `auth`.

## Request

```json
{
  "auth": { "username": "player_one", "token": "<session token>" }
}
```

No required or optional parameters.

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "home_base": {
      "width": 5,
      "height": 5,
      "max_level": 32,
      "can_build_outside_wall": true,
      "display_name": "Manor House",
      "image": "/images/manor/buildings/manor_house_1.png",
      "construction_image": "/images/manor/buildings/manor_house_1-construction.png",
      "construction_times": [60, 20, 40, 120, ...],
      "costs": { "gold": 0, "wood": 0, "stone": 0 },
      "min_manor_level": 1
    },
    "peasant": {
      "width": 3,
      "height": 3,
      "max_level": 5,
      "display_name": "Peasant Cottage",
      "image": "/images/manor/buildings/peasant.png",
      "construction_image": "/images/manor/buildings/peasant-construction.png",
      "construction_times": [30, 35, 50, 70, 90],
      "costs": { "gold": 40, "wood": 20 },
      "min_manor_level": 1
    },
    "build_order": ["road", "woodcutter", "peasant", "wood_hewer", "collier", "blacksmith", "miller", "bloomery", "chapel"]
  }
}
```

### Behavior

- `construction_image` is always present: if the config defines it, that value is used; if not, it's copied from `image`
- `costs` is an object with level-1 costs for `gold`, `wood`, `stone`, `silver_pence` keys (whichever exist)
- `min_manor_level` is extracted from `prerequisites[0].manor_level`, defaults to 1
- `build_order` is an array of building type IDs from `game/config/manor_ui.json`. It governs the **order** of the build-palette buttons in the UI only — the client still applies its own display filters (manor level, `display_name`/`image` presence, affordability, prerequisites, `max_per_fiefdom`). Type IDs not listed sort after every listed one (stable). Omitted/invalid `build_order` returns an empty array and the client falls back to alphabetical order.
- The full building type config from `fiefdom_building_types.json` is included (per-day production fields, daily_cost, modifiers, etc.), including `road_tiles`/`road_tiles_canonical` for road auto-tiling and `road_morale` for morale sources.
- Road config example:
  ```json
  "road": {
    "width": 1, "height": 1, "max_level": 1,
    "display_name": "Road",
    "image": "/images/manor/buildings/road_four_way.png",
    "silver_pence_cost": [1],
    "construction_times": [0],
    "costs": { "silver_pence": 1 },
    "min_manor_level": 1,
    "road_tiles": { "straight": "...", "corner": "...", "three_way": "...", "four_way": "..." },
    "road_tiles_canonical": { "straight": ["e", "w"], "corner": ["n", "e"], "three_way": ["n", "e", "w"] }
  }
  ```

### Error

- Missing/invalid `auth` → `needs_auth` response (same as other authenticated endpoints).
- No config-data errors — this is a config query with no parameters.
