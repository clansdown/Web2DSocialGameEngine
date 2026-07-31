# POST /api/getBuildingConfigs

Get building type configurations for the manor system. No authentication required. No parameters needed.

## Request

```json
{}
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
    }
  }
}
```

### Behavior

- `construction_image` is always present: if the config defines it, that value is used; if not, it's copied from `image`
- `costs` is an object with level-1 costs for `gold`, `wood`, `stone` keys (whichever exist)
- `min_manor_level` is extracted from `prerequisites[0].manor_level`, defaults to 1
- The full building type config from `fiefdom_building_types.json` is included (production fields, hourly_cost, modifiers, etc.)

### Error

None — this is a config query with no parameters.
