# POST /api/getGameInfo

Retrieve all game configuration data including buildings, combatants, heroes, and officials.

## Request

```json
{}
```

The request body can be empty or contain any data - this endpoint is public and does not require authentication.

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "damage_types": ["melee", "ranged", "magical"],
    "fiefdom_building_types": [
      {
        "farm": {
          "width": 2,
          "height": 2,
          "max_level": 5,
          "can_build_outside_wall": true,
          "grain": {...},
          "wood_cost": [5, 6, 7, 8, 9],
          "stone_cost": [10, 12, 14, 16, 18],
          "construction_times": [10, 15, 20, 25, 30]
        }
      },
      {...}
    ],
    "player_combatants": {
      "spearman": {...},
      "archer": {...},
      "knight": {...},
      "mage": {...}
    },
    "enemy_combatants": {
      "goblin": {...},
      "orc": {...},
      "dark_mage": {...},
      "bandit": {...},
      "troll": {...}
    },
    "heroes": {
      "knight_hero": {...},
      "mage_hero": {...},
      "ranger_hero": {...}
    },
    "fiefdom_officials": {
      "henry_wise_steward": {...},
      "elrond_wizard_master": {...},
      {...}
    }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| damage_types | array | List of available damage types |
| fiefdom_building_types | array | Building definitions with costs and stats |
| player_combatants | object | Player-controlled unit definitions |
| enemy_combatants | object | Enemy unit definitions |
| heroes | object | Hero character definitions |
| fiefdom_officials | object | Fiefdom official templates |

### Error (500 Internal Server Error)

```json
{
  "error": "Game configuration not loaded"
```

## Implementation Notes

- **Public endpoint**: Does not require authentication
- **Cached data**: Configuration is loaded once at server startup and cached for all requests
- **Format preserved**: The response matches the structure of the server's JSON configuration files
- **Use case**: Client fetches this once at startup to initialize game data

## Implementation Status

- Configuration loading: Implemented
- Cache initialization: Implemented
- Endpoint handler: Implemented