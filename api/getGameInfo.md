# POST /api/getGameInfo

Retrieve all game configuration data including buildings, combatants, heroes, officials, and their associated images.

**Note:** This endpoint requires authentication. Use `auth` object with username and either password or token.

## Request

### Get all assets (authenticated):
```json
{
  "auth": {
    "username": "player_name",
    "token": "hex_token_value"
  }
}
```

### Get filtered assets by type:
```json
{
  "auth": {
    "username": "player_name",
    "token": "hex_token_value"
  },
  "filters": {
    "asset_types": ["buildings", "combatants"]
  }
}
```

### Get filtered assets by specific IDs:
```json
{
  "auth": {
    "username": "player_name",
    "token": "hex_token_value"
  },
  "filters": {
    "asset_ids": ["farm", "spearman", "knight_hero"]
  }
}
```

## Response

### Success:
```json
{
  "status": "ok",
  "data": {
    "configs": {
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
      },
      "wall_config": {
        "walls": {
          "1": {...},
          "2": {...},
          "3": {...}
        }
      }
    },
    "images": {
      "total_images": 2,
      "buildings": {
        "home_base": {
          "total_images": 2,
          "images": {
            "idle": {
              "count": 1,
              "paths": ["images/buildings/home_base/idle/1.png"]
            },
            "construction": {
              "count": 1,
              "paths": ["images/buildings/home_base/construction/1.png"]
            }
          }
        }
      },
      "combatants": {...},
      "heroes": {...},
      "portraits": {...}
    },
    "token": "hex_token_value"
  }
}
```

## Filter Parameters

| Filter Key | Type | Description |
|------------|------|-------------|
| `asset_types` | array | Filter config and image data by asset types. Valid values: `damage_types`, `fiefdom_building_types`, `player_combatants`, `enemy_combatants`, `heroes`, `fiefdom_officials`, `wall_config`, `buildings`, `combatants`, `portraits`. Config types return config data; image types return image data. |
| `asset_ids` | array | Filter image data by specific asset IDs. Example: `["farm", "barracks", "spearman", "knight_hero"]`. Returns image data only for matching assets across all asset types. |

Both filters are optional. If neither is provided, all configs and all images are returned.

## Image Data Structure

The `images` object contains image file information organized by asset type:

```json
{
  "total_images": <int>,
  "<asset_type>": {
    "<asset_id>": {
      "total_images": <int>,
      "images": {
        "<action>": {
          "count": <int>,
          "paths": ["relative/path/to/image1.png", ...]
        },
        ...
      }
    },
    ...
  }
}
```

### Asset Types and Expected Actions

- **buildings**: `idle`, `construction`, `damaged`, `destroyed`
- **combatants**: `idle`, `attack`, `defend`, `die`
- **heroes**: `idle`, `attack`, `skills/<skill_id>`
- **portraits**: Root level images (no action subdirectory)

## Error Responses

### Authentication Required (401):
```json
{
  "status": "ok",
  "needs-auth": true,
  "auth-failed": false,
  "error": null
}
```

### Configuration Not Loaded (500):
```json
{
  "error": "Game configuration not loaded"
}
```

### Images Not Loaded (500):
```json
{
  "error": "Images not loaded"
}
```

## Implementation Notes

- **Authentication required**: Unlike the previous version, this endpoint now requires valid authentication
- **Cached data**: Configuration and images are loaded once at server startup and cached
- **Image paths**: All paths are relative to the server's images directory
- **Filtering**: Filters rebuild responses from cached data without filesystem operations
- **Use case**: Client fetches this at startup to initialize all game data and asset images
