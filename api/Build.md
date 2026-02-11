# POST /api/Build

Building construction and management.

## Prerequisites

**Important Game Rule:** A fiefdom must have a completed `home_base` (level > 0) before any other buildings can be constructed. The `home_base` must be built at location (0, 0).

## Actions

The `action` field specifies the operation. Default is "create".

| Action | Description |
|--------|-------------|
| `create` | Create a new building at specified location |
| `demolish` | Demolish an existing building with 80% refund |
| `move` | Move an existing building with 10% cost |
| `wall` | Build a wall generation |
| `upgrade` | Upgrade a building or wall to the next level |

### create (default)

Create a new building at specified location.

**Request:**
```json
{
  "fiefdom_id": 1,
  "action": "create",
  "building_type": "farm",
  "x": 2,
  "y": 3
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `fiefdom_id` | integer | Yes | ID of the fiefdom to build in |
| `action` | string | No (default: "create") | Must be "create" |
| `building_type` | string | Yes | Type of building (e.g., "farm", "barracks", "home_base") |
| `x` | integer | Yes | X coordinate relative to fiefdom center |
| `y` | integer | Yes | Y coordinate relative to fiefdom center |

### demolish

Demolish an existing building with 80% refund of cumulative costs (rounded down).

**Request:**
```json
{
  "fiefdom_id": 1,
  "action": "demolish",
  "building_id": 42
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `fiefdom_id` | integer | Yes | ID of the fiefdom |
| `action` | string | Yes | Must be "demolish" |
| `building_id` | integer | Yes | ID of the building to demolish |

**Rules:**
- Cannot demolish home_base (Manor House)
- 80% of cumulative resource costs for levels 1 to current_level are refunded
- Fractional refunds are rounded down

**Response:**
```json
{
  "status": "ok",
  "data": {
    "building_id": 42,
    "refund": {
      "gold": 80,
      "wood": 40
    }
  }
}
```

### move

Move an existing building to a new location with 10% cost (rounded down).

**Request:**
```json
{
  "fiefdom_id": 1,
  "action": "move",
  "building_id": 42,
  "x": 5,
  "y": 7
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `fiefdom_id` | integer | Yes | ID of the fiefdom |
| `action` | string | Yes | Must be "move" |
| `building_id` | integer | Yes | ID of the building to move |
| `x` | integer | Yes | New X coordinate |
| `y` | integer | Yes | New Y coordinate |

**Rules:**
- Cannot move home_base (Manor House)
- Cannot move buildings under construction (level <= 0)
- New location must be valid (no overlaps, within bounds)
- Cost: 10% of current level's construction cost (rounded down)

**Response:**
```json
{
  "status": "ok",
  "data": {
    "building_id": 42,
    "new_x": 5,
    "new_y": 7,
    "cost": {
      "gold": 5,
      "wood": 2
    }
  }
}
```

### wall

Build a wall generation around the fiefdom perimeter. Walls provide defense bonuses and morale boosts.

**Request:**
```json
{
  "fiefdom_id": 1,
  "action": "wall",
  "wall_generation": 1
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `fiefdom_id` | integer | Yes | ID of the fiefdom |
| `action` | string | Yes | Must be "wall" |
| `wall_generation` | integer | Yes | Wall generation to build (1, 2, 3, etc.) |

**Rules:**
- Wall generation 1 has no prerequisites
- Wall generation N+1 requires wall generation N to be completed first
- Each generation can only be built once
- Overlapping buildings are automatically demolished with 80% refund
- Walls are centered at (0, 0) with configurable dimensions

**Response:**
```json
{
  "status": "ok",
  "data": {
    "wall_id": 5,
    "generation": 1,
    "level": 1,
    "hp": 1000,
    "width": 40,
    "length": 40,
    "thickness": 4,
    "cost": {
      "gold": 1000,
      "stone": 500
    },
    "demolished_buildings": [
      {
        "building_id": 42,
        "building_type": "farm",
        "refund": {
          "gold": 80,
          "wood": 40
        }
      }
    ]
  }
}
```

### upgrade

Upgrade an existing building or wall to the next level.

**Request (Building):**
```json
{
  "fiefdom_id": 1,
  "action": "upgrade",
  "building_id": 42
}
```

**Request (Wall):**
```json
{
  "fiefdom_id": 1,
  "action": "upgrade",
  "wall_id": 5
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `fiefdom_id` | integer | Yes | ID of the fiefdom |
| `action` | string | Yes | Must be "upgrade" |
| `building_id` | integer | Yes* | ID of the building to upgrade (exclusive with wall_id) |
| `wall_id` | integer | Yes* | ID of the wall to upgrade (exclusive with building_id) |

**Rules:**
- Either `building_id` or `wall_id` must be provided, but not both
- Cannot upgrade while construction is in progress (level == 0)
- Cannot upgrade beyond max level
- Costs are determined by the next level's configuration
- Construction starts immediately upon successful upgrade request

**Response (Building):**
```json
{
  "status": "ok",
  "data": {
    "building_id": 42,
    "upgrade_to_level": 2,
    "cost": {
      "gold": 500,
      "wood": 250
    }
  }
}
```

**Response (Wall):**
```json
{
  "status": "ok",
  "data": {
    "wall_id": 5,
    "upgrade_to_level": 2,
    "new_hp": 1200,
    "cost": {
      "gold": 800,
      "stone": 400
    }
  }
}
```

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
| `action_required` | action field required | Missing action parameter |
| `invalid_action` | Invalid action type | Must be create/demolish/move |
| `building_type_required` | building_type is required | Missing building_type field |
| `building_id_required` | building_id is required | Required for demolish/move |
| `coordinates_required` | x and y coordinates are required for building placement | Missing x or y coordinates |
| `not_owner` | User does not own this fiefdom | Character doesn't own this fiefdom |
| `unknown_building` | Unknown building type: {type} | Invalid building type |
| `building_not_found` | Building not found | Building ID does not exist |
| `home_base_required` | You must build a Manor House (home_base) before other buildings | Fiefdom has no completed home_base |
| `home_base_exists` | A Manor House (home_base) already exists | Cannot build another home_base |
| `home_base_immutable` | Manor House cannot be demolished or moved | home_base is permanent |
| `invalid_location` | Cannot build at specified location | Invalid coordinates, overlaps with existing buildings, or out of bounds |
| `invalid_home_base_location` | Manor House (home_base) must be built at location (0, 0) | home_base not at center |
| `invalid_config` | Building configuration not found | Building config missing |
| `insufficient_resources` | Not enough resources | Cannot afford construction costs |
| `construction_in_progress` | Building is already under construction | Cannot start new construction while existing construction ongoing |
| `cannot_move_under_construction` | Cannot move building under construction | Building must be completed first |
| `move_location_invalid` | New location not valid for this building | Collision or bounds violation |
| `max_level_reached` | Building is at maximum level | Cannot upgrade further |
| `wall_generation_required` | wall_generation is required | Missing wall_generation field |
| `generation_invalid` | Invalid wall generation: {gen} | Generation not in wall config |
| `generation_sequence_required` | Must build wall generation N first | Lower generation required |
| `generation_exists` | Wall generation N already exists | Cannot rebuild same generation |
| `upgrade_id_required` | Either building_id or wall_id is required | Missing upgrade target |
| `upgrade_in_progress` | Construction already in progress | Cannot upgrade while under construction |
| `not_owner` | User does not own this wall | Character doesn't own this wall |

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

Implemented - Full building construction with position tracking, collision detection, and construction completion via time updates.

**Wall System:** Implemented - Wall generations 1, 2, 3 with configurable dimensions, HP, costs, and morale boosts. Walls are centered at fiefdom origin with automatic building collision resolution.

**Upgrade System:** Implemented - Upgrade buildings and walls to higher levels. Construction starts immediately and completes after the configured time interval.