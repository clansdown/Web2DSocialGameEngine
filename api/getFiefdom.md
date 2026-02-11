# POST /api/getFiefdom

Get fiefdom information including resources, buildings, and officials.

## Request

```json
{
  "fiefdom_id": 1,
  "include_buildings": false,
  "include_officials": false,
  "include_heroes": false,
  "include_combatants": false
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| fiefdom_id | integer | Yes | - | ID of the fiefdom to retrieve |
| include_buildings | boolean | No | false | Include buildings in response |
| include_officials | boolean | No | false | Include officials in response |
| include_heroes | boolean | No | false | Include heroes in response |
| include_combatants | boolean | No | false | Include combatants in response |

## Response

### Success (200 OK) - Minimal

```json
{
  "status": "ok",
  "data": {
    "id": 1,
    "owner_id": 10,
    "name": "My Fiefdom",
    "x": 100,
    "y": 200,
    "peasants": 50,
    "gold": 1000,
    "grain": 500,
    "wood": 300,
    "steel": 100,
    "bronze": 50,
    "stone": 200,
    "leather": 150,
    "mana": 75,
    "wall_count": 3,
    "morale": 0
  }
}
```

### Success (200 OK) - Full

```json
{
  "status": "ok",
  "data": {
    "id": 1,
    "owner_id": 10,
    "name": "My Fiefdom",
    "x": 100,
    "y": 200,
    "peasants": 50,
    "gold": 1000,
    "grain": 500,
    "wood": 300,
    "steel": 100,
    "bronze": 50,
    "stone": 200,
    "leather": 150,
    "mana": 75,
    "wall_count": 3,
    "morale": 0,
    "buildings": [
      {
        "id": 1,
        "name": "Farm",
        "level": 1,
        "construction_start_ts": 1757520000
      },
      {
        "id": 2,
        "name": "Barracks",
        "level": 1,
        "construction_start_ts": 1757520000
      }
    ],
    "officials": [
      {
        "id": 1,
        "role": "Bailiff",
        "template_id": "henry_wise_steward",
        "portrait_id": 100,
        "name": "John",
        "level": 5,
        "intelligence": 150,
        "charisma": 120,
        "wisdom": 130,
        "diligence": 140
      },
      {
        "id": 2,
        "role": "Wizard",
        "template_id": "elrond_wizard_master",
        "portrait_id": 101,
        "name": "Merlin",
        "level": 7,
        "intelligence": 200,
        "charisma": 80,
        "wisdom": 180,
        "diligence": 100
      }
    ],
    "heroes": [
      {
        "id": 1,
        "hero_config_id": "knight_hero",
        "level": 5
      }
    ],
    "stationed_combatants": [
      {
        "id": 1,
        "combatant_config_id": "spearman",
        "level": 3
      },
      {
        "id": 2,
        "combatant_config_id": "archer",
        "level": 2
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Fiefdom ID |
| owner_id | integer | Owning character ID |
| name | string | Fiefdom name |
| x | integer | World X coordinate |
| y | integer | World Y coordinate |
| peasants | integer | Population count |
| gold | integer | Treasury currency |
| grain | integer | Food resource amount |
| wood | integer | Building material amount |
| steel | integer | Military material amount |
| bronze | integer | Alloy material amount |
| stone | integer | Construction material amount |
| leather | integer | Crafting material amount |
| mana | integer | Magical resource amount |
| wall_count | integer | Defensive wall layers |
| morale | number | Fiefdom morale score (-1000 to 1000) |
| buildings | array | Building instances (when include_buildings=true) |
| officials | array | Official instances (when include_officials=true) |
| heroes | array | Hero instances (when include_heroes=true) |
| stationed_combatants | array | Combatant instances (when include_combatants=true) |

### Building Object

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Building ID |
| name | string | Building name |
| level | integer | Building level (0 = under construction) |
| construction_start_ts | integer | Epoch timestamp when construction started |

### Official Object

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Official ID |
| role | string | Official role (Bailiff, Wizard, Architect, Steward, Reeve, Beadle, Constable, Forester) |
| template_id | string | Config template ID |
| portrait_id | integer | Portrait asset ID |
| name | string | Official's name |
| level | integer | Official experience level |
| intelligence | integer | Mental stat (0-255) |
| charisma | integer | Leadership stat (0-255) |
| wisdom | integer | Judgment stat (0-255) |
| diligence | integer | Work ethic stat (0-255) |

### Hero Object

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Hero record ID |
| hero_config_id | string | Config ID (e.g., "knight_hero") |
| level | integer | Hero level |

### Combatant Object

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Combatant record ID |
| combatant_config_id | string | Config ID (e.g., "spearman") |
| level | integer | Combatant level |

### Error (400 Bad Request)

```json
{
  "error": "fiefdom_id required"
}
```

### Error (404 Not Found)

```json
{
  "error": "fiefdom not found"
}
```

## Implementation Status

- **Database query**: Implemented via `FiefdomFetcher::fetchFiefdomById()`
- **Data structures**: `FiefdomData`, `BuildingData`, `OfficialData` in `FiefdomData.hpp`
- **Fetching logic**: `FiefdomFetcher.cpp` handles all database queries
- **Resources**: Full resource tracking (peasants, gold, grain, wood, steel, bronze, stone, leather, mana)
- **Buildings**: Generic building support with level and construction timestamps
- **Officials**: All 8 official roles with full stat tracking (intelligence, charisma, wisdom, diligence)
- **Optional includes**: Buildings, officials, heroes, and combatants can be optionally included via request parameters
- **Performance**: Database queries skipped when optional data is not requested