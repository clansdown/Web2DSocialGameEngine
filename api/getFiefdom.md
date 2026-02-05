# POST /api/getFiefdom

Get fiefdom information including resources, buildings, and officials.

## Request

```json
{
  "fiefdom_id": 1
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| fiefdom_id | integer | Yes | ID of the fiefdom to retrieve |

## Response

### Success (200 OK)

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
    "buildings": [
      {
        "id": 1,
        "name": "Farm"
      },
      {
        "id": 2,
        "name": "Barracks"
      }
    ],
    "officials": [
      {
        "id": 1,
        "role": "Bailiff",
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
        "portrait_id": 101,
        "name": "Merlin",
        "level": 7,
        "intelligence": 200,
        "charisma": 80,
        "wisdom": 180,
        "diligence": 100
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
| buildings | array | List of buildings in fiefdom |
| officials | array | List of officials serving fiefdom |

### Building Object

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Building ID |
| name | string | Building name |

### Official Object

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Official ID |
| role | string | Official role (Bailiff, Wizard, Architect, Steward, Reeve, Beadle, Constable, Forester) |
| portrait_id | integer | Portrait asset ID |
| name | string | Official's name |
| level | integer | Official experience level |
| intelligence | integer | Mental stat (0-255) |
| charisma | integer | Leadership stat (0-255) |
| wisdom | integer | Judgment stat (0-255) |
| diligence | integer | Work ethic stat (0-255) |

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
- **Buildings**: Generic building support (name-based)
- **Officials**: All 8 official roles with full stat tracking (intelligence, charisma, wisdom, diligence)