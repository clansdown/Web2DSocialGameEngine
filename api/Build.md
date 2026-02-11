# POST /api/Build

Building construction and management.

## Prerequisites

**Important Game Rule:** A fiefdom must have a completed `home_base` (level > 0) before any other buildings can be constructed. The `home_base` must be built at location (0, 0).

## Request

```json
{
  "fiefdom_id": 1,
  "building_type": "farm",
  "x": 2,
  "y": 3
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `fiefdom_id` | integer | Yes | ID of the fiefdom to build in |
| `building_type` | string | Yes | Type of building (e.g., "farm", "barracks", "home_base") |
| `x` | integer | Yes | X coordinate relative to fiefdom center (required) |
| `y` | integer | Yes | Y coordinate relative to fiefdom center (required) |

## Coordinate System

Coordinates are relative to the fiefdom center (0, 0):
- Positive X extends to the right
- Positive Y extends upward
- Buildings occupy squares based on their width/height from config

**Special rules:**
- `home_base` must be at (0, 0)
- Buildings cannot overlap
- Buildings are placed with their bottom-left corner at (x, y)

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "building_type": "farm",
    "fiefdom_id": 1,
    "x": 2,
    "y": 3,
    "construction_start_ts": 1699999999,
    "level": 0
  }
}
```

### Error (400 Bad Request)

```json
{
  "error": "error_message"
}
```

## Error Codes

| Error Code | Message | Description |
|------------|---------|-------------|
| `fiefdom_id_required` | fiefdom_id is required | Missing fiefdom_id field |
| `building_type_required` | building_type is required | Missing building_type field |
| `coordinates_required` | x and y coordinates are required for building placement | Missing x or y coordinates |
| `not_owner` | User does not own this fiefdom | Character doesn't own this fiefdom |
| `unknown_building` | Unknown building type: {type} | Invalid building type |
| `home_base_required` | You must build a Manor House (home_base) before other buildings | Fiefdom has no completed home_base |
| `home_base_exists` | A Manor House (home_base) already exists | Cannot build another home_base |
| `invalid_location` | Cannot build at specified location | Invalid coordinates, overlaps with existing buildings, or out of bounds |
| `invalid_home_base_location` | Manor House (home_base) must be built at location (0, 0) | home_base not at center |
| `invalid_config` | Building configuration not found | Building config missing |
| `insufficient_resources` | Not enough resources | Cannot afford construction costs |
| `construction_in_progress` | Building is already under construction | Cannot start new construction while existing construction ongoing |
| `max_level_reached` | Building is at maximum level | Cannot upgrade further |

## Building Construction

When a building is constructed:
1. Resources are deducted from the fiefdom
2. A building record is created at level 0 (under construction)
3. `construction_start_ts` is set to the current timestamp
4. Construction time is determined by the building's `construction_times` config array
5. Once construction time elapses, the building automatically upgrades to level 1

The `/api/updateState` endpoint handles construction completion during time progression.

## Home Base Special Rules

The `home_base` building (displayed as "Manor House") has special rules:

1. **Only one per fiefdom** - Attempting to build a second home_base returns `home_base_exists` error
2. **Must be at center** - Must be built at coordinates (0, 0)
3. **No prerequisites** - Home base can be built first (no other buildings required)
4. **Configurable** - All properties (size, max level, costs, construction time) are defined in config

## Implementation Status

Implemented - Full building construction with position tracking, collision detection, and construction completion via time updates