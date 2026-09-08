# fiefdom_buildings Table

Stores buildings constructed within a fiefdom. Buildings represent player-constructed infrastructure that contributes to fiefdom functionality.

## Schema

```sql
CREATE TABLE fiefdom_buildings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    level INTEGER NOT NULL DEFAULT 0,
    x INTEGER NOT NULL DEFAULT 0,
    y INTEGER NOT NULL DEFAULT 0,
    construction_start_ts INTEGER NOT NULL DEFAULT 0,
    last_updated INTEGER NOT NULL DEFAULT 0,
    action_start_ts INTEGER NOT NULL DEFAULT 0,
    action_tag TEXT NOT NULL DEFAULT '',
    pond_type TEXT NOT NULL DEFAULT '',
    output_rates TEXT NOT NULL DEFAULT '{}',
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique building identifier |
| fiefdom_id | INTEGER | NOT NULL FK | Parent fiefdom (fiefdoms.id) |
| name | TEXT | NOT NULL | Building type name (e.g., "farm", "home_base"). On a stage conversion (`/api/Build` action `convert`) this changes to the successor stage's id. |
| level | INTEGER | NOT NULL DEFAULT 0 | Building level (0 = under construction, 1+ = active) |
| x | INTEGER | NOT NULL DEFAULT 0 | X coordinate relative to fiefdom center |
| y | INTEGER | NOT NULL DEFAULT 0 | Y coordinate relative to fiefdom center |
| construction_start_ts | INTEGER | NOT NULL DEFAULT 0 | Unix timestamp when construction started (0 if not under construction) |
| last_updated | INTEGER | NOT NULL DEFAULT 0 | Unix timestamp of last update (production, construction completion) |
| action_start_ts | INTEGER | NOT NULL DEFAULT 0 | Reserved for future action system |
| action_tag | TEXT | NOT NULL DEFAULT '' | Reserved for future action system |
| pond_type | TEXT | NOT NULL DEFAULT '' | Mill pond type: "earthen"/"timber"/"stone" (empty for non-ponds). Levels are within the type; the type upgrade is `/api/Build` action `upgrade_pond_type`. |
| output_rates | TEXT | NOT NULL DEFAULT '{}' | JSON object mapping output resource → player rate (0..1). Missing entries default to 1.0. Set via `/api/setBuildingOutputRate`. |
| tech_xp | REAL | NOT NULL DEFAULT 0 | Accumulated technology XP (passive accrual from `tech_xp_per_day[level-1]`, + completed training grants) |
| tech_nodes | TEXT | NOT NULL DEFAULT '[]' | JSON array of learned tech-tree node ids (`/api/learnTechNode`) |
| forge_order | TEXT | NOT NULL DEFAULT '' | JSON `{item_id, start_ts, duration_hours}` of the active forge order, or `''` (mints an armory item on completion) |
| training | TEXT | NOT NULL DEFAULT '' | JSON `{grant_xp, start_ts, duration_hours}` of the active training timer, or `''` (teacher/book grant lands on completion) |

## Indexes

- Index on `fiefdom_id` for building lookups by fiefdom
- Index on `fiefdom_id, x, y` for spatial queries and collision detection

## Relationships

- Many-to-one with `fiefdoms` via `fiefdom_id` foreign key
- Each fiefdom can have zero or more buildings

## Coordinate System

Buildings are positioned using a grid system relative to the fiefdom center:
- `x`: Horizontal position (right is positive)
- `y`: Vertical position (up is positive)

Each building occupies a rectangle of tiles based on its type:
- Width and height are defined in `fiefdom_building_types.json`
- Buildings are placed with their bottom-left corner at (x, y)
- Collision detection prevents overlapping buildings

## Construction System

**Level 0** = Under Construction
- Building exists but produces no resources
- `construction_start_ts` tracks when construction began
- Building is not operational until construction completes

**Level 1+** = Active Building
- Building produces resources based on its config
- Can be upgraded to higher levels

## Automatic Construction Completion

When `/api/updateState` is called:
1. Server calculates time elapsed since `construction_start_ts`
2. Compares against `construction_times` from building config for current level
3. If sufficient time has passed, level is incremented and `construction_start_ts` set to 0
4. Building becomes active and produces resources

## Notes

- Building names correspond to types in `fiefdom_building_types.json`. Production
  lines are **stage chains** (`built_from`); a building's `name` identifies its
  current stage. Prerequisite/dependency counting is chain-aware: a building
  satisfies requirements for itself and every lower stage in its chain (a
  `villein`/`yeoman` counts as a `peasant`). Converting in place changes `name`,
  resets `level` (0 = re-constructing), clears `pond_type`/`output_rates`, and
  preserves x/y.
- Buildings are fetched alongside fiefdom data via `/api/getFiefdom`
- Up to 255 buildings per fiefdom (SQLite INTEGER range)
- The `home_base` building must be at coordinates (0, 0) and only one can exist per fiefdom

## Roads

Roads are ordinary rows in this table with `name = 'road'`:
- 1×1 tile (`width`/`height` 1, `max_level` 1), instant build (`construction_times[0] == 0`),
  costing 1 silver pence (`silver_pence_cost: [1]`, deducted from `fiefdoms.silver_pence`).
- Placed paint-style anywhere; collision only blocks other buildings. A 1×1 road at `(x, y)`
  occupies exactly that cell.
- They produce nothing; they carry road-network morale from morale-source buildings
  (chapel/miller) to nearby buildings, boosting their production. See
  `server/docs/fiefdom_building_types.md` → "Roads & Road-Network Morale".
- Level is always ≥ 1 (instant build), so roads never appear under construction.

## Usage Examples

Fetching buildings for a fiefdom:

```cpp
std::vector<BuildingData> buildings;
db << "SELECT id, name, level, x, y, construction_start_ts, last_updated FROM fiefdom_buildings WHERE fiefdom_id = ?;"
   << fiefdom_id
   >> [&](int id, std::string name, int level, int x, int y, int64_t construction_start_ts, int64_t last_updated) {
       buildings.push_back({id, name, level, x, y, construction_start_ts, 0, last_updated, ""});
   };
```

Creating a new building:

```cpp
int64_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
db << "INSERT INTO fiefdom_buildings (fiefdom_id, name, level, x, y, construction_start_ts, last_updated) VALUES (?, ?, 0, ?, ?, ?, ?);"
   << fiefdom_id << "Farm" << x << y << now << now;
```

Checking construction completion:

```cpp
db << "UPDATE fiefdom_buildings SET level = ?, construction_start_ts = ? WHERE id = ?;"
   << new_level << 0 << building_id;
```