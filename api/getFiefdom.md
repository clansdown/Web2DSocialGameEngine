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
    "silver_pence": 456,
    "grain": 500,
    "wood": 300,
    "steel": 100,
    "bronze": 50,
    "stone": 200,
    "leather": 150,
    "mana": 75,
    "charcoal": 40,
    "iron": 25,
    "wall_count": 3,
    "morale": 0,
    "manor_level": 1
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
    "silver_pence": 456,
    "grain": 500,
    "wood": 300,
    "steel": 100,
    "bronze": 50,
    "stone": 200,
    "leather": 150,
    "mana": 75,
    "charcoal": 40,
    "iron": 25,
    "ironwork": 10,
    "fancy_ironwork": 3,
    "wall_count": 3,
    "morale": 0,
    "manor_level": 1,
    "reserves": {
      "grain": 150,
      "wood": 100,
      "steel": 50,
      "bronze": 25,
      "stone": 60,
      "leather": 25,
      "mana": 10,
      "charcoal": 50,
      "iron": 50,
      "ironwork": 50
    },
    "economy_report": {
      "elapsed_seconds": 3600,
      "produced": { "steel": 40.0, "charcoal": 90.0 },
      "consumed": { "iron": 20.0, "charcoal": 20.0, "grain": 20.0 },
      "imported": { "iron": 20.0 },
      "exported": {
        "grain": { "amount": 150.0, "pence": 900 }
      },
      "net_gold": 12.5,
      "net_silver": 900,
      "recommendations": [
        "You imported 20 iron this period. A bloomery would produce iron locally.",
        "A blacksmith only ran at 80% capacity — check its input supply."
      ]
    },
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
| gold | integer | Treasury currency (building costs, non-penny imports) |
| silver_pence | integer | Silver-pence wallet (mini-game rewards + penny-market grain sales) |
| grain | integer | Food resource amount |
| wood | integer | Building material amount |
| steel | integer | Military material amount |
| bronze | integer | Alloy material amount |
| stone | integer | Construction material amount |
| leather | integer | Crafting material amount |
| mana | integer | Magical resource amount |
| charcoal | integer | Fuel resource amount (collier output, blacksmith input) |
| iron | integer | Ore resource amount (blacksmith input) |
| ironwork | integer | Refined military material (blacksmith output, unit upkeep) |
| fancy_ironwork | integer | Fine tempered iron (blacksmith level 2+ output) |
| wall_count | integer | Defensive wall layers |
| morale | number | Fiefdom morale score (-1000 to 1000) |
| manor_level | integer | Manor house upgrade level (default 1) |
| reserves | object | Effective per-resource reserves (overrides merged with defaults) |
| economy_report | object | Per-update economy ledger (produced/consumed/imported/exported, net_gold, recommendations) |
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
| output_rates | object | Map of output resource → player rate (0..1). Missing entries default to 1.0. |

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

### economy_report Object

| Field | Type | Description |
|-------|------|-------------|
| elapsed_seconds | integer | Seconds covered by this update |
| produced | object | Resource → gross amount produced this period (before input gating) |
| consumed | object | Resource → amount consumed (building inputs + daily costs + population costs + combatant upkeep) |
| imported | object | Resource → amount bought via auto-import this period |
| exported | object | Resource → `{ amount, gold }` (gold market) or `{ amount, pence }` (penny market, e.g. grain) sold above the reserve this period |
| net_gold | number | Gold produced + export gold − import spend − gold consumed (purse net) |
| net_silver | number | Pence exports − pence imports (penny-market net; absent when no penny trade) |
| recommendations | array | Server-side economic advice strings |

Buildings with an `inputs` config consume those resources per day; output is scaled by the
min input-satisfaction ratio across all inputs (1.0 when no inputs). Shortfalls are auto-imported
when the resource's import setting is enabled (full-buy default) — paid in gold for most resources
and in silver pence for penny-market resources like grain. Stationed combatants
consume `upkeep` resources per day. After production/consumption/imports, any resource above its
reserve is auto-sold at its export price — explicit `export_prices[resource]`, else
`export_sell_multipliers[resource]` × import price, else `export_sell_multiplier` (0.5) × import
price (grain sells for 6 pence each, into `silver_pence`); amounts at or below reserve are kept.

### reserves Object

Effective per-resource reserves (stored overrides merged with `economy.json.default_reserves`).
Stock above a resource's reserve is auto-sold; set via `/api/setFiefdomReserve`.

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
- **Resources**: Full resource tracking (peasants, gold, grain, wood, steel, bronze, stone, leather, mana, charcoal, iron)
- **Buildings**: Generic building support with level and construction timestamps
- **Officials**: All 8 official roles with full stat tracking (intelligence, charisma, wisdom, diligence)
- **Optional includes**: Buildings, officials, heroes, and combatants can be optionally included via request parameters
- **Performance**: Database queries skipped when optional data is not requested
- **Production updates**: Automatically applied before returning data. Pending resource production and construction completions are calculated based on `last_update_time` and applied to ensure client receives the latest state. Uses 1-second minimum guard to prevent excessive CPU/disk I/O from rapid requests.
- **Economy report**: Each update returns an `economy_report` ledger with production, consumption, imports, exports, net gold, and advisor recommendations.