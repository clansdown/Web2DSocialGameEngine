# Fiefdom Building Types Configuration

Configuration file: `server/config/fiefdom_building_types.json`

This file defines all available building types in the game. It uses lenient JSON parsing (allows comments, trailing commas) via nlohmann/json with `ignore_comments=true`.

## TypeScript Type Definition

```typescript
interface ResourceProduction {
    amount?: number;
    amount_multiplier?: number;
    periodicity?: number;
    periodicity_multiplier?: number;
}

interface FiefdomBuildingType {
    // --- Resource Production (all optional, defaults to 0) ---
    peasants?: ResourceProduction;
    gold?: ResourceProduction;
    grain?: ResourceProduction;
    wood?: ResourceProduction;
    steel?: ResourceProduction;
    bronze?: ResourceProduction;
    stone?: ResourceProduction;
    leather?: ResourceProduction;
    mana?: ResourceProduction;

    // --- Construction Costs (all optional, defaults to empty array) ---
    peasants_cost?: number[];
    gold_cost?: number[];
    grain_cost?: number[];
    wood_cost?: number[];
    steel_cost?: number[];
    bronze_cost?: number[];
    stone_cost?: number[];
    leather_cost?: number[];
    mana_cost?: number[];

    // --- Required Structural Fields ---
    width: number;
    height: number;
    max_level: number;

    // --- Optional Structural Fields ---
    can_build_outside_wall?: boolean;  // Defaults to false
    display_name?: string;              // User-facing name (e.g., "Manor House")

    // --- Construction ---
    construction_times: number[];       // Seconds per level (index = level)
}
```

## Field Descriptions

### Resource Production Fields

All resource production fields follow the same structure and are optional. If omitted, the resource produces nothing.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `amount` | number | 0 | Base production amount at level 1 |
| `amount_multiplier` | number | 0 | Multiplier applied per level (compounding) |
| `periodicity` | number | 0 | Base time between productions in seconds at level 1 |
| `periodicity_multiplier` | number | 0 | Periodicity multiplier applied per level (compounding) |

### Cost Arrays (construction costs)

Cost arrays specify the resource cost per building level. Index corresponds to level (0-indexed).

| Field | Type | Description |
|-------|------|-------------|
| `*_cost` | number[] | Array of resource costs per level |

### Structural Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `width` | number | required | Building width in tiles |
| `height` | number | required | Building height in tiles |
| `max_level` | number | required | Maximum level this building can attain |
| `can_build_outside_wall` | boolean | false | Whether this building can be placed outside fiefdom walls |
| `display_name` | string | inferred from ID | User-facing display name (e.g., "Manor House") |

### Construction Fields

| Field | Type | Description |
|-------|------|-------------|
| `construction_times` | number[] | Seconds required for construction at each level |

## Extrapolation Rules

Both `construction_times` and all cost arrays use the same extrapolation logic:

| Array Length | Behavior |
|--------------|----------|
| 0 elements | No cost / instant building; implies max 1 level possible |
| 1 element | Use value for level 0, linear extrapolation with slope of 1 |
| 2+ elements | Linear extrapolation from last two elements |

### Extrapolation Formula

For arrays with 2+ elements at index `i >= (length - 2)`:

```
value[i] = value[last] + (i - last) * (value[last] - value[last-1])
```

Where `last = length - 1` and `last-1 = length - 2`.

### Example

Given `construction_times: [10, 15, 20]` with `max_level: 5`:

| Level | Construction Time (seconds) |
|-------|----------------------------|
| 0 | 10 |
| 1 | 15 |
| 2 | 20 |
| 3 | 25 (20 + 5) |
| 4 | 30 (25 + 5) |
| 5+ | 35, 40, ... (continues with slope 5) |

## Resource Production Calculation

For a resource at level `L` (1-indexed, where level 0 is under construction):

```
amount = base_amount * (amount_multiplier ^ (L - 1))
periodicity = base_periodicity * (periodicity_multiplier ^ (L - 1))
```

### Example

For a building with:
- `grain: { amount: 10, amount_multiplier: 1.2, periodicity: 60, periodicity_multiplier: 1.05 }`

| Level | Amount | Periodicity |
|-------|--------|-------------|
| 1 | 10.0 | 60.0s |
| 2 | 12.0 | 63.0s |
| 3 | 14.4 | 66.2s |
| 4 | 17.3 | 69.5s |
| 5 | 20.7 | 73.0s |

## Default Values

If a field is not specified, the following defaults apply:

| Field | Default |
|-------|---------|
| `can_build_outside_wall` | `false` |
| Any `ResourceProduction` field | All four properties default to `0` |
| Any `*_cost` array | `[]` (empty array, free) |

## Image Auto-Detection

Building images are auto-detected from directory structure:

```
server/images/buildings/{building_id}/
    construction/1.png, 2.png, ...  # required (non-empty)
    idle/1.png, 2.png, ...           # required (non-empty)
    harvest/1.png, 2.png, ...        # optional (non-empty)
```

- **construction/**: Required - shows building under construction animation
- **idle/**: Required - shows building in idle state
- **harvest/**: Optional - shows production/harvest animation (some buildings may not need this)

**Filename Convention:**
- Files are named with numeric prefixes: `1.png`, `2.png`, `3.png`, etc.
- Server walks this directory at startup to auto-detect available images
- No image filenames need to be specified in config files

**Linter Validation:**
The `check_configs.py` tool validates the images directory:
- **Errors:** Empty required directories
- **Warnings:** Missing directories, empty optional directories, orphaned files

## JSON Config Structure

```json
[
    {
        "farm": {
            "width": 2,
            "height": 2,
            "max_level": 5,
            "can_build_outside_wall": true,
            "grain": {
                "amount": 10,
                "amount_multiplier": 1.2,
                "periodicity": 60,
                "periodicity_multiplier": 1.05
            },
            "wood_cost": [5, 6, 7, 8, 9],
            "stone_cost": [10, 12, 14, 16, 18],
            "construction_times": [10, 15, 20, 25, 30]
        }
    }
]
```

## Lenient Parsing

The config file is parsed using nlohmann/json with lenient settings:

```cpp
nlohmann::json data = nlohmann::json::parse(content, nullptr, true, true);
//                                      ^           ^    ^    ^
//                                      content   cb  allow  ignore
//                                                except comments
```

This allows:
- C-style comments: `// comment` or `/* comment */`
- Trailing commas in arrays and objects
- Relaxed JSON syntax

## Resource Types

Available resource types for both production and costs:

| Resource | Type | Description |
|----------|------|-------------|
| `peasants` | production only | Population/labor |
| `gold` | both | Currency |
| `grain` | both | Food |
| `wood` | both | Building material |
| `steel` | both | Military material |
| `bronze` | both | Alloy material |
| `stone` | both | Construction material |
| `leather` | both | Crafting material |
| `mana` | production only | Magical resource |

## Building IDs

The building type ID is used as the key in the JSON array entries. All IDs must be unique and use lowercase with underscores (snake_case).

Current building types:

| ID | Name | Size | Max Level | Wall | Primary Production |
|----|------|------|-----------|------|--------------------|
| `farm` | Farm | 2x2 | 5 | No | grain |
| `barracks` | Barracks | 3x3 | 10 | Yes | peasants |
| `home_base` | Manor House | 3x3 | 32 | No | none (required prerequisite) |
| `mine` | Mine | 2x2 | 7 | No | stone, steel |
| `sawmill` | Sawmill | 3x2 | 5 | No | wood |
| `house` | House | 4x4 | 3 | Yes | all resources (small) |

## Level 0 Clarification

Level 0 represents a building **under construction**. During this level:
- The building produces nothing
- Construction time has elapsed
- Player sees construction animation
- Building cannot be used for production

Level 1+ represents active, producing buildings.

## Client-Side Notes

The following fields are used by the client for visual rendering:
- `width` and `height` (for grid placement)

Images are auto-detected from `server/images/buildings/{building_id}/` directory structure:
- `construction/` for construction animation
- `idle/` for idle animation
- `harvest/` for production animation (optional)

The server walks the images directory at startup - no image paths in config.