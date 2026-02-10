# POST /api/executeAction

Execute any registered action with validation.

## Request

```json
{
  "auth": {
    "username": "player1",
    "token": "existing-token"
  },
  "action_type": "build",
  "fiefdom_id": 1,
  "building_type": "farm",
  "x": 5,
  "y": 5
}
```

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "building_type": "farm",
    "fiefdom_id": 1,
    "construction_start_ts": 1757520000,
    "level": 0,
    "side_effects": [
      {
        "field": "gold",
        "source_type": "fiefdom",
        "source_id": 1,
        "entity_key": "fiefdom_id",
        "from_value": 50,
        "to_value": 40
      },
      {
        "field": "wood",
        "source_type": "fiefdom",
        "source_id": 1,
        "entity_key": "fiefdom_id",
        "from_value": 10,
        "to_value": 5
      }
    ],
    "action_timestamp": 1757520000
  },
  "token": "new-token-if-refreshed"
}
```

### Error (400 Bad Request)

```json
{
  "error": "fiefdom_id_required"
}
```

## Available Actions

| Action Type | Description |
|-------------|-------------|
| build | Build/upgrade structures at location |
| build_wall | Build/upgrade walls at fiefdom border |
| train_troops | Train combatants (STUB) |
| research_magic | Research magic abilities (STUB) |
| research_tech | Research technologies (STUB) |

## Action Parameters

### build

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| fiefdom_id | integer | Yes | Target fiefdom ID |
| building_type | string | Yes | Building type from config (e.g., "farm", "barracks") |
| x | integer | No | X coordinate for building placement |
| y | integer | No | Y coordinate for building placement |

### build_wall

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| fiefdom_id | integer | Yes | Target fiefdom ID |

## Error Codes

| Code | Description |
|------|-------------|
| fiefdom_id_required | fiefdom_id field is required |
| building_type_required | building_type field is required |
| not_owner | User does not own this fiefdom |
| unknown_building | Building type does not exist in config |
| invalid_location | Cannot build at specified location |
| insufficient_resources | Not enough resources for action |
| missing_wall_config | Wall configuration not found |
| invalid_wall_placement | Cannot place wall at this location |
| database_error | Database operation failed |

## Side Effects

The `side_effects` array contains detailed diffs for all resource changes caused by the action. Each entry includes:

| Field | Type | Description |
|-------|------|-------------|
| field | string | Resource or field that changed |
| source_type | string | Type of entity (e.g., "fiefdom", "building") |
| source_id | integer | ID of the entity |
| entity_key | string | Key to identify entity type |
| from_value | number | Value before action |
| to_value | number | Value after action |

## Implementation Status

- Building structures: Implemented
- Building walls: Implemented
- Training troops: STUB - Not yet implemented
- Magic research: STUB - Not yet implemented
- Technology research: STUB - Not yet implemented