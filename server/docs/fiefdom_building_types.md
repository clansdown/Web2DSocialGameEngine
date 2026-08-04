# Fiefdom Building Types Configuration

Configuration file: `game/config/fiefdom_building_types.json`

This file defines all available building types in the game. It uses lenient JSON parsing (allows comments, trailing commas) via nlohmann/json with `ignore_comments=true`.

## TypeScript Type Definition

```typescript
interface ResourceProduction {
    amount?: number;  // Quantity produced/consumed per 1-day period
}

interface MoneyCost {
    gold?: number;      // Gold pieces (pounds)
    shillings?: number; // Silver shillings (20 per pound)
    pence?: number;     // Silver pence (6 per shilling)
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
    charcoal?: ResourceProduction;
    iron?: ResourceProduction;
    ironwork?: ResourceProduction;

    // --- Economic Inputs (optional) ---
    inputs?: {
        [resource: string]: ResourceProduction;
    };

    // --- Construction Costs (all optional, defaults to empty array) ---
    peasants_cost?: number[];
    gold_cost?: (number | MoneyCost)[];
    grain_cost?: number[];
    wood_cost?: number[];
    steel_cost?: number[];
    bronze_cost?: number[];
    stone_cost?: number[];
    leather_cost?: number[];
    mana_cost?: number[];
    charcoal_cost?: number[];
    iron_cost?: number[];
    ironwork_cost?: number[];

    // --- Required Structural Fields ---
    width: number;
    height: number;
    max_level: number;

    // --- Optional Structural Fields ---
    can_build_outside_wall?: boolean;  // Defaults to false
    display_name?: string;              // User-facing name (e.g., "Manor House")
    image?: string;                     // Client-side sprite path (e.g., "/images/manor/buildings/blacksmith.png")

    // --- Construction ---
    construction_times: number[];       // Seconds per level (index = level)

    // --- Prerequisites (optional, defaults to no prerequisites) ---
    prerequisites?: PrerequisiteObject[];  // Required buildings per level
}
```

## Field Descriptions

### Resource Production Fields

All resource production fields follow the same structure and are optional. If omitted, the resource produces nothing.

All production and consumption share a fixed **1-day period**: `amount` is the quantity
produced/consumed **per day**. The engine scales linearly with fractional elapsed days
(`amount × days_elapsed`), so any elapsed time beyond a tiny minimum (≈1 second) yields a
proportional amount — nothing is floored to whole cycles.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `amount` | number | 0 | Quantity produced/consumed per 1-day period |

> **Removed fields:** the legacy `amount_multiplier`, `periodicity`, and
> `periodicity_multiplier` per-cycle fields are gone (artifacts of an earlier computation
> model). Building-to-building multipliers live in the `modifiers` array instead.

### Economic Inputs Field

The optional `inputs` object defines the resources a building **consumes** to produce its outputs.
Inputs use the same `ResourceProduction` structure as outputs, so they are expressed as **per-day**
consumption on the same 1-day period the economy engine uses for production.

```json
{
    "blacksmith": {
        "outputs": [
            {
                "resource": "ironwork",
                "amount": 100,
                "inputs": {
                    "grain":    { "amount": 100 },
                    "charcoal": { "amount": 100 },
                    "iron":     { "amount": 100 }
                },
                "min_level": 1
            },
            {
                "resource": "grain",
                "amount": 18,
                "min_level": 1
            },
            {
                "resource": "fancy_ironwork",
                "amount": 1,
                "inputs": {
                    "grain":    { "amount": 1 },
                    "charcoal": { "amount": 1 },
                    "iron":     { "amount": 2 }
                },
                "min_level": 2
            }
        ]
    },
    "collier": {
        "charcoal": { "amount": 30 },
        "grain":    { "amount": 18 },
        "inputs": {
            "wood": { "amount": 20 }
        }
    },
    "bloomery": {
        "iron": { "amount": 20 },
        "inputs": {
            "charcoal": { "amount": 30 }
        }
    }
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `inputs` | object | omitted | Map of resource → `ResourceProduction` spec. Each entry is consumed per day. |

### How Input Gating Works

1. Each update, the engine computes each building's required inputs using the same flat per-day
   formula as production (based on fractional elapsed days).
2. Inputs are folded into the priority-sorted consumption pipeline alongside `daily_cost`,
   population costs, and combatant upkeep (`player_combatants.json` `upkeep` arrays for stationed
   units). Stock is drawn first; any shortfall is auto-imported with gold when that resource's
   import setting is enabled (full-buy default), and remaining unmet needs cause morale penalties.
3. Each building's output is **scaled by its input-satisfaction ratio** — the minimum across its
   inputs of (supplied ÷ required). A blacksmith with half its iron produces half its ironwork.
4. Buildings without `inputs` produce at 100%.
5. After production, consumption, and imports, any resource **above its reserve** is auto-sold at
   50% of the import price (`economy.json.export_sell_multiplier`); amounts at or below the reserve
   are kept. Reserves default from `economy.json.default_reserves` and can be overridden per fiefdom
   via `/api/setFiefdomReserve`.

The per-update `economy_report` in `/api/getFiefdom` exposes the resulting produced/consumed/imported/
exported amounts, `net_gold` (production minus imports), and advisor recommendations.

### Cost Arrays (construction costs)

Cost arrays specify the resource cost per building level. Index corresponds to level (0-indexed).

| Field | Type | Description |
|-------|------|-------------|
| `*_cost` | number[] | Array of resource costs per level |
| `gold_cost` | (number \| MoneyCost)[] | Gold cost per level — each element is either a plain number (gold) or a `{ gold, shillings, pence }` object (all keys optional, non-negative) |

A `MoneyCost` object is normalized to a gold double at config load:
`gold + shillings/20 + pence/240` (1 gold = 20 shillings = 240 pence; 1 shilling = 12 pence).
It is purely an authoring convenience — all cost readers, refunds, the client, and the economy see a plain gold number afterwards.

Example — a manor house costing 100 gold at level 1 and 1 gold 10s 5d at level 2:

```json
"gold_cost": [
    100,
    { "gold": 1, "shillings": 10, "pence": 5 },
    200
]
```

### Max Per Fiefdom

The optional `max_per_fiefdom` field limits how many buildings of this type can exist in a single fiefdom.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_per_fiefdom` | integer | unlimited | Maximum number of this building per fiefdom (e.g. `1` = unique building) |

- Enforced at build time by the server (`build` action). Attempting to place another instance returns error `max_per_fiefdom_reached`.
- Buildings without this field can be built any number of times.
- The `home_base` uniqueness rule is separate (hardcoded, with stricter placement rules).
- Example: a chapel is limited to one per fiefdom:
  ```json
  "chapel": {
      "max_per_fiefdom": 1
  }
  ```

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
  - **Keys**: Building type IDs (e.g., "home_base", "farm", "barracks") or special keys like `manor_level`
  - **Values**: Minimum required level for that building/manor
  - `manor_level` is a special key — checks the fiefdom's manor_level instead of a building level

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

## Dependencies Field

The optional `dependencies` field defines **count-based building requirements**. Unlike `prerequisites` (which check if a building type exists at a minimum level), dependencies check **how many** buildings of a given type exist in the fiefdom.

### TypeScript Definition

```typescript
interface BuildingDependency {
    target_building: string;  // Building type ID to count
    count: number;            // How many are required
    shared: boolean;          // If true, counts toward shared pool; if false, exclusive
    min_level?: number;       // Minimum level of target buildings to count (default 1)
}
```

### Config Format

The `dependencies` field is an array indexed by building level (same pattern as `construction_times` and `prerequisites`). Each entry is an array of dependency objects:

```json
{
    "sawyer": {
        "dependencies": [
            [{"target_building": "peasant", "count": 5, "shared": false, "min_level": 2}],   // Level 1 (build)
            [{"target_building": "peasant", "count": 5, "shared": false}],                    // Level 2
            [{"target_building": "peasant", "count": 10, "shared": false}]                    // Level 3
        ]
    }
}
```

### Shared vs Exclusive

The `shared` flag determines whether a building's count can overlap with other dependencies:

| Scenario | Shared | Exclusive |
|----------|--------|-----------|
| Manor needs 20 peasants | 20 | — |
| Sawyer needs 5 peasants | — | 5 |
| **Total needed** | **max(20,5) = 20** | **20 + 5 = 25** |

- **Shared**: All shared dependencies for the same target_building share one pool. Total needed = `max(all shared counts)`.
- **Exclusive**: Each exclusive dependency adds to the total. Total needed = `sum(all exclusive counts)` + `max(all shared counts)`.

### Level Arrays

Both `count` and `min_level` support level-indexed arrays with linear extrapolation (same rules as `construction_times`).

### Extrapolation Rules

If the `dependencies` array is shorter than the building's `max_level`, the last entry is used for all remaining levels (same as prerequisites).

### Check Points

Dependencies are verified at:
1. **Build time** — when constructing a new building (level 1)
2. **Upgrade time** — when upgrading a building (checked against next level's dependencies)
3. **Construction completion** — when `construction_start_ts` elapses and the building levels up

## Resource Production Calculation

Production and consumption are **flat per-day rates**. Given a resource spec with `amount`, the
quantity over an elapsed period is:

```
produced = amount × (elapsed_seconds / 86400)
```

Fractional elapsed days are used directly — nothing is floored to whole cycles, so a 12-hour gap
yields exactly half a day's production. Elapsed time below a tiny minimum (≈1 second) is ignored
to avoid rounding errors. Building level does **not** scale production; upgrading gates
construction, costs, prerequisites, and `modifiers`, not output rates.

### Example

For a building with `grain: { amount: 10 }`:

| Elapsed | Amount Produced |
|---------|-----------------|
| 12 hours | 5.0 |
| 1 day | 10.0 |
| 2.5 days | 25.0 |

## Default Values

If a field is not specified, the following defaults apply:

| Field | Default |
|-------|---------|
| `can_build_outside_wall` | `false` |
| Any `ResourceProduction` field | `amount` defaults to `0` |
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
                "amount": 10
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
| `charcoal` | both | Fuel (collier output, blacksmith input) |
| `iron` | both | Ore (blacksmith input; bloomery produces) |
| `ironwork` | both | Forged metal tools (blacksmith output, building upkeep + combatant upkeep) |
| `fancy_ironwork` | both | Fine tempered iron (blacksmith level 2+ output) |

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

## Daily Cost Field

The optional `daily_cost` and `priority` fields define a building's ongoing resource consumption. Consumption is processed by the economy engine each time fiefdom state updates, scaled by fractional elapsed days.

> **Note:** For input-gated production, prefer the `inputs` field above. `daily_cost` is a flat
> per-day rate (not input-gated) and is satisfied alongside inputs. Buildings may use both.

```json
{
    "blacksmith": {
        "daily_cost": { "grain": 36 },
        "priority": 50
    },
    "peasant": {
        "grain": { "amount": 74 },
        "daily_cost": { "grain": 36, "ironwork": 1 },
        "priority": 5
    }
}
```

The Peasant Cottage produces **74 grain/day** and costs **36 grain/day** in
upkeep (its household's food), for a net food surplus of 38 grain/day once
populated, plus **1 ironwork/day** for its tools. Grain is a penny-market
resource (`economy.json import_prices.grain` is a money object), so imports are
paid in silver pence and surplus grain auto-sells for 6 pence each (50% of the
1-shilling import price).

### Ironwork upkeep & household grain

Every active craft building is a self-contained household. It **produces 18
grain/day** (a subsistence plot) and **consumes 36 grain/day** (worker food,
`daily_cost`), and it requires a small **ironwork/day** `daily_cost` for its
metal tools. Current per-building ironwork upkeep:

| Building | ironwork/day |
|----------|--------------|
| home_base | 20 |
| peasant | 1 |
| woodcutter | 5 |
| wood_hewer | 5 |
| miller | 5 |
| collier | 2 |

One blacksmith produces **100 ironwork/day** (input-gated on grain 100 +
charcoal 100 + iron 100 per day), which covers roughly 50 peasants, 2 woodcutters,
2 woodhewers, a miller, a few colliers, and the home base (~100 ironwork/day).
The blacksmith's own household produces grain 18/day and consumes grain
36/day. Ironwork is **imported at 0.02 gold/unit** and **exports for 25% of the
import price** (0.005 gold — `economy.json.export_sell_multipliers.ironwork =
0.25`), so it is meant to be produced, not sold.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `daily_cost` | object | omitted | Map of resource → per-day consumption rate. Resources are consumed continuously (scaled by fractional days). |
| `priority` | integer | `economy.json.default_priority` (50) | Allocation priority. Lower numbers are satisfied first when resources are scarce. |

### How Consumption Works

1. All consumption entries from all buildings + population costs are collected
2. Sorted by priority ascending
3. Resources are allocated in priority order — high-priority consumers (peasants at 5) get their needs met before low-priority ones (blacksmith at 50)
4. If resources run out, unmet needs cause morale penalties
5. Players can toggle auto-import per resource to fill deficits — gold for most
   resources, silver pence for penny-market resources (money-form import prices)

### Outputs (multi-output recipes)

The optional `outputs` array defines a building's production as a set of
recipes, each with its **own inputs** and an **unlock level**. This replaces the
flat `<resource>: { amount }` fields + building-level `inputs` (a building may
use one schema or the other — mixing them is a config error).

```json
{
    "blacksmith": {
        "outputs": [
            {
                "resource": "ironwork",
                "amount": 2,
                "inputs": { "grain": { "amount": 1 }, "charcoal": { "amount": 1 }, "iron": { "amount": 1 } },
                "min_level": 1
            },
            {
                "resource": "fancy_ironwork",
                "amount": 1,
                "inputs": { "grain": { "amount": 1 }, "charcoal": { "amount": 1 }, "iron": { "amount": 2 } },
                "min_level": 2
            }
        ]
    }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `resource` | string | Produced resource (a valid production resource, e.g. `ironwork`, `fancy_ironwork`) |
| `amount` | number | Maximum per-day output (scaled by fractional elapsed days, modifiers, and the player rate) |
| `inputs` | object | This output's own per-day input requirements; each value is a `{ amount }` spec |
| `min_level` | integer | Building level at which the output unlocks (default 1) |

A building at level < `min_level` cannot produce that output and consumes none of
its inputs. At level ≥ `min_level`, every unlocked output runs at its **player
rate** (default 1.0 = full). The player sets the rate per output via
`/api/setBuildingOutputRate` (0..1); the rate scales both the output amount and
that output's input requirements, and a rate of 0 turns the output fully off.
Each output is gated by its **own** input-satisfaction ratio, so the blacksmith
can keep forging `ironwork` while `fancy_ironwork` idles for want of iron.

Example — blacksmith level 1 produces only `ironwork` (needs 100 iron/day);
at level 2 it can also run `fancy_ironwork` (needs 2 iron/day) at the same time.

### Population Costs

Global population costs are defined in `economy.json` under `population_costs`. These consume resources based on the fiefdom's population count, scaled by fractional elapsed days:

```json
{
    "population_costs": {
        "peasants": { "grain": 0.5, "priority": 1 }
    }
}
```

This per-peasant grain cost is separate from the Peasant Cottage's own
`daily_cost`; it represents the extra food each additional population unit eats
on top of the cottage's 36-grain household upkeep.

## Modifiers Field

The optional `modifiers` field defines building-to-building production boosts. One building can increase another building's resource production output.

### TypeScript Definition

```typescript
interface BuildingModifier {
    modifier_id: string;         // Unique ID for stacking prevention
    target_building: string;     // Building type ID to boost (e.g., "woodcutter")
    target_resource: string;     // Resource to multiply (e.g., "wood", "grain")
    multiplier: number | number[];  // Production multiplier (5.0 = 500% = +400%)
    max_targets: number | number[]; // Max buildings this can boost
}
```

### Field Descriptions

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `modifier_id` | string | required | Unique identifier for this modifier. Two sources with the same ID cannot boost the same target (prevents stacking duplicates) |
| `target_building` | string | required | Building type ID that receives the boost |
| `target_resource` | string | required | Resource production to multiply (e.g., `"wood"`, `"grain"`, `"gold"`) |
| `multiplier` | number or number[] | required | Total production multiplier. `5.0` means the target produces `5×` their base. Can be an array indexed by level for per-level scaling |
| `max_targets` | number or number[] | required | Maximum number of buildings this can simultaneously boost. Can be an array indexed by level. `1` = single-target, `[100, 150]` = 100 at level 1, 150 at level 2 |

### Array Scaling Rules

Both `multiplier` and `max_targets` accept:
- A single number (constant across all building levels)
- An array indexed by building level (level 0 → index 0). Linear extrapolation when level exceeds array size (same rules as `construction_times`)

### Stacking Rules

1. **Same `modifier_id`**: Only one source can boost a given target (e.g., two wood hewers with `sharpen_axes` cannot both boost the same woodcutter). Excess boosters distribute across unboosted targets, up to their `max_targets`.
2. **Different `modifier_id`s**: Multipliers stack multiplicatively (e.g., wood hewer `5.0` + sawyer `1.5` → `7.5×` total)
3. **Capacity resolution**: Higher-level boosters get priority when assigning to targets

### Assignment Algorithm

```
For each modifier_id group:
  1. Collect all source buildings with this modifier (level > 0)
  2. Sort sources by level descending
  3. Collect all target buildings (level > 0) of target_building type
  4. For each source (in priority order):
     - Determine effective max_targets from source's level
     - Assign to unboosted targets up to max_targets
     - Each target can be boosted at most once per modifier_id
```

### Example Configs

```json
// Wood hewer boosts 1 woodcutter's wood production by 400% (5×)
"modifiers": [{
    "modifier_id": "sharpen_axes",
    "target_building": "woodcutter",
    "target_resource": "wood",
    "multiplier": 5.0,
    "max_targets": 1
}]

// Miller boosts peasant grain production, capacity scales with level
"modifiers": [{
    "modifier_id": "flour_milling",
    "target_building": "peasant",
    "target_resource": "grain",
    "multiplier": 3.0,
    "max_targets": [100, 150, 200, 250, 300]
}]
```

## Client-Side Notes

The following fields are used by the client for visual rendering:
- `width` and `height` (for grid placement)

Images are auto-detected from `server/images/buildings/{building_id}/` directory structure:
- `construction/` for construction animation
- `idle/` for idle animation
- `harvest/` for production animation (optional)

The server walks the images directory at startup - no image paths in config.