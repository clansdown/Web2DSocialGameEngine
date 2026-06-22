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

interface PrerequisiteObject {
    [buildingId: string]: number;  // Required building type ID -> minimum level
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

    // --- Prerequisites (optional, defaults to no prerequisites) ---
    prerequisites?: PrerequisiteObject[];  // Required buildings per level
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

### Prerequisites Field

The `prerequisites` field defines required buildings and their minimum levels for constructing or upgrading a building.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `prerequisites` | PrerequisiteObject[] | none | Array of prerequisite requirements per level |

#### Prerequisites Array Structure

- Array index corresponds to building level (like `construction_times`)
- Index 0 = requirements for building to reach level 1 (first active level)
- Each array element is an object where:
  - **Keys**: Building type IDs (e.g., "home_base", "farm", "barracks")
  - **Values**: Minimum required level for that building

#### Prerequisites Extrapolation Rules

Prerequisites follow the same interpolation rules as other arrays-for-levels:

| Array Length | Behavior |
|--------------|----------|
| 0 elements | No prerequisites at any level (building can always be built/upgraded) |
| 1 element | Constant prerequisites for all levels |
| 2+ elements | Linear extrapolation from last two elements |

#### Prerequisites Interpolation Formula

For each building ID in prerequisites, the required level is extrapolated:

```
required_level[i] = level[last] + (i - last) * (level[last] - level[last-1])
```

Where `last = array_length - 1` and `last-1 = array_length - 2`.

#### Prerequisites Examples

**Example 1: No prerequisites**
```json
"prerequisites": [{}]
```
- Building can be constructed at any time (only needs home_base check)
- All upgrades available without additional requirements

**Example 2: Level 1+ requires home_base**
```json
"prerequisites": [{"home_base": 1}]
```
- Level 0 (construction): Requires home_base level 1
- Level 1+: Same requirement (constant from 1 element)

**Example 3: Escalating requirements**
```json
"prerequisites": [
    {},
    {"farm": 2},
    {"farm": 3, "sawmill": 1}
]
```
- Level 0: No prerequisites (empty object)
- Level 1: Requires farm at level 2
- Level 2+: Requires farm level 3 AND sawmill level 1
- Level 3+: Extrapolated (farm: 3, sawmill: 1 - constant since only 2 elements)

**Example 4: Complete chain**
```json
"barracks": {
    "prerequisites": [
        {},
        {"farm": 2},
        {"farm": 3, "sawmill": 1}
    ]
}
```
- Build barracks at level 0: No prerequisites (just home_base)
- Upgrade to level 1: Need farm at level 2
- Upgrade to level 2+: Need farm at level 3 AND sawmill at level 1

#### Validation Behavior

Prerequisites are checked at three points:
1. **Building creation**: Checks if prerequisites for level 1 are met
2. **Building upgrade**: Checks if prerequisites for target level are met
3. **Construction completion**: If prerequisites become unmet during construction, the upgrade fails, resources are refunded, and the building reverts to under construction

#### Prerequisite Failure

If a building's prerequisites are not met during construction completion:
1. The building remains at level 0 (under construction)
2. Resources for the attempted level are refunded to the fiefdom
3. The failure is logged in the update result

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

### Required Building ID: home_base

The `home_base` building type (Manor House) is **mandatory** and has special game rules:

| Property | Requirement | Description |
|----------|-------------|-------------|
| **ID key** | `home_base` | Must be present in config |
| **Mandatory fields** | `width`, `height`, `max_level`, `construction_times` | Required structural fields for validation |
| **Placement** | Coordinates (0, 0) | Fixed location in the center of fiefdom |
| **Max per fiefdom** | 1 | Only one home_base allowed |
| **Immutable** | Yes | Cannot be demolished or moved |
| **Prerequisite** | Required before other buildings | Must exist at level > 0 before any other building can be constructed |
| **display_name** | "Manor House" or equivalent | User-facing name for UI |

The server enforces these rules with specific error codes:

| Error Code | Meaning | Returned When |
|------------|---------|---------------|
| `home_base_required` | Manor House must be built first | Attempting to build any other building before a completed home_base exists |
| `home_base_exists` | Cannot build another | Attempting to build a second home_base |
| `home_base_immutable` | Cannot demolish/move | Attempting to demolish or move the existing home_base |
| `invalid_home_base_location` | Must be at (0,0) | Attempting to build home_base away from center |

See `api/Build.md` for complete building API documentation including home_base rules.

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