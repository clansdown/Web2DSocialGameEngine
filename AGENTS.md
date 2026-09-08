# AGENTS.md

## Documentation Policy

When working on this project:

1. **Consult component-specific README files first**:
   - See `server/README.md` for server architecture, auth system, and design decisions
   - Component READMEs are the primary source of architectural context

2. **API documentation required for all endpoints**:
   - Every new API endpoint MUST have a corresponding `.md` file in `api/`
   - Document request format, response format, and error cases
   - See existing `api/*.md` files for format reference

3. **Update documentation when making changes**:
   - Endpoint changes → update both endpoint file AND server README
   - Authentication changes → update server README auth section
   - Schema changes → update `server/tables/*.md` files
   - New dependencies → update CMakeLists.txt AND dependencies list

4. **Document your decisions**:
   - Explain WHY choices were made (not just WHAT)
   - Include tradeoff considerations for future reference
   - Mark unimplemented features with "TODO: ..." placeholder

This maintains living documentation that grows with the codebase.

## Server

The server is a C++23 application built with CMake that:
- Uses uWebSockets for HTTP endpoint handling
- Uses nlohmann/json for JSON request/response parsing
- Uses sqlite_modern_cpp for SQLite database operations
- Listens on port 2290 for incoming requests (HTTP only)
- Designed to be deployed behind nginx or other HTTPS reverse proxies

### Dependencies
- **uWebSockets**: High-performance async web server framework
- **nlohmann/json**: Modern JSON library for C++
- **sqlite_modern_cpp**: Header-only SQLite C++ wrapper
- **SQLite3**: Embedded database (system library)

### Documentation References

- **uWebSockets**: https://unetworking.github.io/uWebSockets.js/generated/index.html
- **nlohmann/json**: https://github.com/nlohmann/json
- **sqlite_modern_cpp**: https://github.com/SqliteModernCpp/sqlite_modern_cpp

### Table Documentation

The `server/tables/` directory contains detailed schema documentation for each SQL table:
- `server/tables/users.md` - users table
- `server/tables/characters.md` - characters table (renamed from players)
- `server/tables/fiefdoms.md` - fiefdoms table
- `server/tables/fiefdom_buildings.md` - fiefdom_buildings table (level, x/y, construction, `pond_type`, `output_rates`)
- `server/tables/fiefdom_river.md` - fiefdom_river table (water-power river cells)
- `server/tables/player_messages.md` - player_messages table
- `server/tables/message_queues.md` - message_queues table
- `server/tables/player_game_state.md` - player_game_state table (game phases, baron-track honor name)

Each `.md` file documents the table's purpose, full schema, field descriptions, indexes, relationships, and usage notes. See `server/tables/README.md` for guidance on documenting new tables.

### Database Architecture

The server uses **two independent SQLite databases** for maximum concurrency:

1. **game.db** - Game state and persistent data
   - `users`: User accounts and authentication
   - `characters`: Character entities with display names and levels
   - `fiefdoms`: Character territories and holdings
   - Future game tables as needed

2. **messages.db** - Character messaging system
   - `player_messages`: Direct messages between characters
   - `message_queues`: Unread message counters per character
   - Future messaging tables as needed

**Design Notes:**
- No joins across databases - they are completely independent
- Concurrent writes possible to both databases simultaneously
- Separate file handles allow true parallel access
- Database files created automatically in working directory

### game.db Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    adult INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE characters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    display_name TEXT NOT NULL,
    safe_display_name TEXT NOT NULL,
    level INTEGER DEFAULT 1,
    FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE fiefdoms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL,
    FOREIGN KEY(owner_id) REFERENCES characters(id)
);
```

**Note:** The full `fiefdoms` schema (see `server/tables/fiefdoms.md`) also includes resource
columns — `gold, silver_pence, grain, wood, steel, bronze, leather, mana, charcoal, iron,
ironwork, fancy_ironwork, beams, boards` (the `stone` column is **retired** — stone costs were
removed from the game, no building produces or consumes it, and it stays 0) — plus `wall_count`, `morale`, `last_update_time`, and `import_settings`. `gold` is
read/written as a double (fractional values allowed); `silver_pence` is fractional-capable (REAL)
to support half-penny prices (beams 4d/1d, boards 1.5d/0.5d). `charcoal` is produced by the collier and
consumed by the blacksmith and bloomery; `iron` is produced by the bloomery and consumed by the
blacksmith; `ironwork` is produced by the blacksmith and consumed by building upkeep and unit
upkeep; `fancy_ironwork`
is produced by the blacksmith's level-2+ `fancy_ironwork` output; `beams` is produced by the wood
hewer chain (`wood_hewer`→`hewing_shop`→`master_hewer`, consuming wood) and `boards` by the sawyer
chain (`sawyer`→`saw_yard`→`saw_mill`, consuming wood) — both consumed as build materials
(`beams_cost`/`boards_cost`). Craft buildings are self-contained
households: each (peasant cottage, blacksmith, collier, woodcutter, wood_hewer) produces **18 grain/day**
(subsistence plot) and consumes **36 grain/day** via `daily_cost`, plus a small `daily_cost` of
`ironwork` (tools: home_base 20, peasant 1, woodcutter 5, wood_hewer 5, miller 5, collier 2). One
blacksmith produces **100 ironwork/day** (input-gated on charcoal 100 + iron 40 per day),
covering ~50 peasants + 2 woodcutters + 2 woodhewers + a miller + a few colliers + the home base.
All production
and consumption share a fixed **1-day period** (`amount` = per day, scaled by fractional elapsed
days — no per-cycle `periodicity`/`amount_multiplier` fields). Production is defined by the
`outputs` array — the **only** production schema: each output has its own `inputs`, a `min_level`
unlock, and a per-player rate (0..1) stored in `fiefdom_buildings.output_rates` (set via
`/api/setBuildingOutputRate`). Flat `<resource>: {amount}` fields and a building-level `inputs`
map are disallowed (config lint enforces this). Inputs are consumed **once per building per
day** (not multiplied by the number of outputs), and each output is gated by its own
input-satisfaction ratio.
**Arable land** (`arable_acres` per building type) is an **abstract resource**
limiting manor growth — never rendered and no DB column. Total acres scale with
`manor_level` via `economy.json` `arable_land_by_level` (index 0–10: 0 at level 0,
400 at level 1, → 1000 at level 10). The **manor house's `max_level` is 10** and
`manor_level` = the home_base's level, so `manor_level` ranges **0–10** (new
fiefdoms default to 0; home_base under construction). Used = Σ each completed
building's `arable_acres` (15 villein, 30 freeholder/yeoman, 7 for the
18-grain craft households — blacksmith/collier/woodcutter/wood_hewer/sawyer +
upgraded stages, 0 for infra/industrial/modifier). `/api/Build` and stage
**convert** reject with `insufficient_arable_land` when available < claimed;
demolishing frees acres. `/api/getFiefdom` returns `arable_land:
{ total, used, available }`; `/api/getBuildingConfigs` injects `arable_acres`.
**Forest land** (`forest_acres` per type) is the same off-map mechanism for the
wood producers only (woodcutter 80, coppicer 60, timber_hauler 70); total scales
with `manor_level` via `forest_land_by_level` (200 at level 1 → 600 at level 10),
gated by `insufficient_forest_land`, reported as `forest_land: { total, used,
available }`, and injected as `forest_acres`. **Every building type carries a
`class` string** grouping it definitively (independent of the `built_from` chain):
`peasant` = villein/freeholder/yeoman, `flourmill` = miller/windmill/watermill
(`mill`), etc. — modifiers and prerequisites match a `class` target in addition
to chain matching. Stage chains gate later stages by manor level (stage 2 = 3,
stage 3 = 6).
`reserves` (a JSON object)
stores per-resource stockpile minimums; excess above a reserve is auto-sold at the resource's
export price — `export_prices[resource]` (explicit), else `export_sell_multipliers[resource]`
(per-resource ratio), else `export_sell_multiplier` (0.5) × import price.
`import_prices` values are plain numbers (gold-denominated) or money objects `{gold, shillings,
pence}` — the latter are **penny-market** resources paid in `silver_pence` (grain is `{"shillings":
1}`, imported at 1 shilling / sold at 6 pence). Import prices **deflate as production scales up**
(anchored to grain, 1 shilling ≈ 0.05 gold): wood 0.03, charcoal 0.03, iron 0.06, ironwork 0.02.
`ironwork` exports sell at **25% of its import price** (`export_sell_multipliers.ironwork = 0.25`);
other resources sell at 50%. Currency ratios are standard medieval:
12 pence/shillng, 20 shillings/pound, 240 pence/gold.
`economy.json.starting_resources` defines the per-resource starting balances for a **newly created
fiefdom** (currently `gold: 5`, all others 0) — the single source of truth shared by the server's
fiefdom creation and the balance analyzer's `--sim`.

**Money is fungible**: `gold` and `silver_pence` are one wallet at 1 gold = 240 pence (the `currency`
block), so a cost or import denominated in one can be paid from the other (converting at that rate).
Penny-market resources (grain, wood, beams, boards, ironwork, ...) can therefore be bought with gold
when a fiefdom has no silver. **Build/upgrade/convert auto-import material shortfalls**: missing
physical build materials are purchased at their import price with fungible money (respecting the
per-resource `import_settings` toggle), and the economy tick's penny-market imports can draw on gold.
This applies consistently in the server (`hasEnoughResources`/`deductResources`/`supply_need`) and the
balance analyzer's `--sim` (`is_feasible`/`commit_build`/`supply_need`). The single source of truth is
the **generic money layer** in `server/Money.hpp/.cpp` (`money::wallet`, `money::load_currency`,
`money::affordable`, `money::pay`, `money::take_pence`/`take_gold`, `money::price_to_pence`):
`Validation::hasEnoughResources`/`deductResources` and the economy-tick import/export/gold-upkeep math
delegate to it, and hire fees route through the same fungible, auto-importing path.

### messages.db Schema

```sql
CREATE TABLE player_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    from_character_id INTEGER NOT NULL,
    to_character_id INTEGER NOT NULL,
    message TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    read INTEGER DEFAULT 0
);

CREATE TABLE message_queues (
    character_id INTEGER PRIMARY KEY NOT NULL,
    unread_count INTEGER DEFAULT 0
);
```

### game.db Additional Tables

```sql
CREATE TABLE game_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    mini_game TEXT NOT NULL,
    level_id INTEGER NOT NULL DEFAULT 0,
    started_at INTEGER NOT NULL,
    last_activity INTEGER NOT NULL,
    total_rounds INTEGER NOT NULL DEFAULT 1,
    current_round INTEGER NOT NULL DEFAULT 0,
    difficulty INTEGER NOT NULL DEFAULT 1,
    lives INTEGER NOT NULL DEFAULT 20,
    gold INTEGER NOT NULL DEFAULT 100,
    state TEXT NOT NULL DEFAULT 'active',
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

Used for ongoing tower defense game sessions. Created on tdRound kickoff, updated on completion.

```sql
CREATE TABLE fiefdom_river (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id),
    UNIQUE(fiefdom_id, x, y)
);
```

Per-fiefdom river cells for the water-power system (mill pond + head/tail races
+ water-powered buildings). Seeded lazily from `manor_river.json` templates
(rotated 0/90/180/270° per fiefdom) by `FiefdomFetcher::ensureFiefdomRiver`.
Buildings may never overlap river cells. See `server/tables/fiefdom_river.md`.

**Water-power model** (`Water::computeWaterPower`): a `water_source` building
(the 8×8 `mill_pond`, types earthen/timber/stone with capacity 2/4/6) is active
when its footprint touches a river cell; head races (wooden, `2w + 2d`, 10s
build) BFS-reach water-powered buildings from the pond; tail races (`1d`,
instant) must reach the river. A `water_powered` building is powered iff
head-reached from a pond with spare capacity AND tail-reached from the river;
unpowered water buildings produce nothing (daily_cost still applies).
`fiefdom_buildings.pond_type`
stores the pond's type; `level` is within the type (type upgrade = `/api/Build`
action `upgrade_pond_type`). Pond cost/construction arrays resolve from
`pond_types[pond_type]` via `Validation::getBuildingArrayField` /
`getNextLevelCost` / `getBuildingMaxLevel`.

**Stage chains (`built_from`)**: production building lines are chains of
independent building types linked by the config field `built_from` (unbounded
depth; "3 stages" is a design default, not a limit — the peasant chain is 4 deep:
peasant → villein → freeholder → yeoman). Every stage is independently buildable
(gated by its own `prerequisites[0]`); a completed building can be converted in
place to its successor via the `/api/Build` action `convert` at
`max(0, successor_lvl1_cost − 80% × old_cumulative_cost)` per resource (any
level 1–5). Prerequisite/dependency/modifier-target counting is **chain-aware**:
a building satisfies requirements for itself and every lower stage in its chain
(a villein/yeoman counts as a peasant; never the reverse). Production amounts and
inputs are **level-indexed arrays** (authoring convention: +5%/level, +20%/stage);
`daily_cost` and the 18-grain household outputs stay flat.
`amount` accepts numbers, money objects `{gold, shillings, pence}` (normalized to
gold), or arrays of either. Build costs support the production resources too
(`charcoal_cost`, `iron_cost`, `ironwork_cost`, `fancy_ironwork_cost`). The
fiefdom's `manor_level` is set to the manor house's (`home_base`) level on
construction completion, gating `{"manor_level": N}` prerequisites.

### API Endpoints

All endpoints accept POST requests with JSON bodies and respond with:
- Success: `{ "status": "ok", "data": {...} }`
- Error: `{ "error": "error_message_string" }`

**IMPORTANT:** Each API endpoint has corresponding documentation in `api/` directory:
- See `api/login.md` for `/api/login` documentation
- See `api/getCharacter.md` for `/api/getCharacter` documentation
- See `api/Build.md` for `/api/Build` documentation
- See `api/getWorld.md` for `/api/getWorld` documentation
- See `api/getFiefdom.md` for `/api/getFiefdom` documentation
- See `api/sally.md` for `/api/sally` documentation
- See `api/campaign.md` for `/api/campaign` documentation
- See `api/hunt.md` for `/api/hunt` documentation
- See `api/updateUserProfile.md` for `/api/updateUserProfile` documentation
- See `api/updateCharacterProfile.md` for `/api/updateCharacterProfile` documentation
- See `api/tdRound.md` for `/api/tdRound` documentation
- See `api/getTexts.md` for `/api/getTexts` documentation
- See `api/getCharacterTexts.md` for `/api/getCharacterTexts` documentation
- See `api/setFiefdomReserve.md` for `/api/setFiefdomReserve` documentation
- See `api/setBuildingOutputRate.md` for `/api/setBuildingOutputRate` documentation
- See `api/combatCreate.md` for `/api/combatCreate` documentation
- See `api/combatJoin.md` for `/api/combatJoin` documentation
- See `api/combatList.md` for `/api/combatList` documentation
- See `api/combatGetConfigs.md` for `/api/combatGetConfigs` documentation
- See `api/combatMatchmaking.md` for `/api/combatMatchmaking` documentation
- See `api/getRetinue.md` for `/api/getRetinue` documentation
- See `api/listRecruitCandidates.md` for `/api/listRecruitCandidates` documentation
- See `api/hireRecruit.md` for `/api/hireRecruit` documentation
- See `api/setRetinuePriority.md` for `/api/setRetinuePriority` documentation
- See `api/getTechTrees.md` for `/api/getTechTrees` documentation
- See `api/learnTechNode.md` for `/api/learnTechNode` documentation
- See `api/startForgeOrder.md` for `/api/startForgeOrder` documentation
- See `api/getRetinueGear.md` for `/api/getRetinueGear` documentation
- See `api/equipGear.md` for `/api/equipGear` documentation
- See `api/deassignGear.md` for `/api/deassignGear` documentation
- See `api/sellItem.md` for `/api/sellItem` documentation
- See `api/buyGearCity.md` for `/api/buyGearCity` documentation
- See `api/hireTeacher.md` for `/api/hireTeacher` documentation
- See `api/applyBookItem.md` for `/api/applyBookItem` documentation

#### Endpoint Overview

- **/api/login**: Authenticate user and return all characters
- **/api/getCharacter**: Retrieve character information
- **/api/updateUserProfile**: Update user account settings (adult flag)
- **/api/updateCharacterProfile**: Update character profile (display names)
- **/api/Build**: Building construction/management (actions: `build`/`create`, `demolish`, `move`, `upgrade`, `upgrade_pond_type`; buildings may never overlap river cells)
- **/api/getWorld**: Get world state (STUB - TODO: implement)
- **/api/getFiefdom**: Get fiefdom information (buildings, economy report, `river_cells`, `water_power`, `road_morale`, `arable_land`)
- **/api/sally**: Sally forth/battle actions (STUB - TODO: implement)
- **/api/campaign**: Campaign management (STUB - TODO: implement)
- **/api/hunt**: Hunting activities (STUB - TODO: implement)
- **/api/tdRound**: Tower defense round lifecycle (kickoff + completion)
- **/api/getTexts**: Public text fetch (pre-auth screens, no substitution)
- **/api/getCharacterTexts**: Character-context text fetch (server applies gender + `{character_name}` substitution)
- **/api/setFiefdomReserve**: Set per-resource stockpile reserves (excess above reserve is auto-sold at the resource's export price — explicit `export_prices`, else `export_sell_multipliers` ratio, else 50% of import price)
- **/api/setBuildingOutputRate**: Set a building output's production rate (0..1) per building instance — scales that output and its inputs; validates the output exists and is unlocked at the building's level
- **/api/combatCreate**: Create a realtime combat lobby (PvE) — returns a shareable match code
- **/api/combatJoin**: Join a combat lobby by code
- **/api/combatList**: List open combat lobbies
- **/api/combatGetConfigs**: List combat rulesets and maps
- **/api/combatMatchmaking**: PvP matchmaking (STUB — challenges/acceptances later)
- **/api/getRetinue**: Fetch the character's retinue (knight + soldiers + capacity)
- **/api/listRecruitCandidates**: List the current hire market (named candidate offers)
- **/api/hireRecruit**: Hire a candidate (verified offer, capacity, fee from fiefdom stores)
- **/api/setRetinuePriority**: Reorder the roster by strict priority (maintenance + infirmary-bed order; an ordered permutation of the non-knight members)
- **/api/getTechTrees**: Tech trees + per-building XP/learned nodes/active forge & training
- **/api/learnTechNode**: Spend building tech XP to learn a node (prereq-gated, saves)
- **/api/startForgeOrder**: Start a forge order at a tech-capable building (pays materials)
- **/api/getRetinueGear**: The fiefdom's armory (gear) + general storage (items)
- **/api/equipGear**: Equip an armory item to a member (level/slot/class checked)
- **/api/deassignGear**: Return a member's equipped slot to the armory
- **/api/sellItem**: Sell an armory item or storage item at the config sell discount
- **/api/buyGearCity**: Buy gear from the city at an extreme markup (gold-sink bypass)
- **/api/hireTeacher**: Start a training timer on a tech building (on-demand, pays gold)
- **/api/applyBookItem**: Consume a training item from storage to start a training timer

### Realtime Combat

The combat game (PvE 1–32; PvP 2–64 later) is a server-authoritative RTS over
**WebSocket `/ws/combat`** on port 2290 — **not** a mini-game. Design and
protocol: `docs/combat_protocol.md`; mission rules: `docs/combat_rulesets.md`;
map format: `docs/combat_maps.md`.

- Matches live **entirely in RAM** (`server/combat/`); SQLite is only touched
  after a battle ends (casualty persistence deferred to the uWS loop thread).
- One worker thread per battle from a bounded pool (`--combat-sim-threads`);
  the loop thread only enqueues commands and relays chat/voice.
- 10 ticks/s simulation; entity-level deltas + full snapshots every 25 ticks;
  clients interpolate and can `request_state`.
- Codec is pluggable (`combat_codec`); `json_codec` default, `binary_codec`
  placeholder. Never build wire format inside game logic.
- Retinue: `retinue_members` table (knight = character). Units are created by
  the manor game's `train_troops` (stub). See `server/tables/retinue_members.md`.
- Voice chat: WebRTC mesh; server relays signaling only. TURN required in prod.

### Building

#### Quick Compile Check

Run with **no arguments** (default/normal output) to test compilation:

```bash
./compile_server.sh          # Compile with normal output (default)
./compile_server.sh -q       # Compile silently (quiet mode)
./compile_server.sh -v       # Compile with verbose output
./compile_server.sh --help   # Show all options
```

Exit codes:
- `0`: Compilation successful
- `1`: Build failure

#### Manual Build

```bash
cd server
mkdir -p build && cd build
cmake ..
make
./server
```

### Docker

```bash
cd server
docker build -t ravenest-server .
docker run -p 2290:2290 ravenest-server
```

### Testing the Server

When testing or verifying the server, use the `agent_test_server.sh` script for automated testing:

```bash
./agent_test_server.sh -N 100        # Process 100 requests then exit
./agent_test_server.sh -M 30         # Run for 30 seconds then exit
./agent_test_server.sh -N 50 -M 60   # Exit on whichever limit reached first
```

**Script Options:**
- `-N N`: Exit after N requests (required or use -M)
- `-M M`: Exit after M seconds (required or use -N)
- `-p PORT`: Port to bind (default: 3290)
- `-l FILE`: Write logs to file
- `-q`: Quiet mode (minimal output)
- `-v`: Verbose mode
- `--keep-db`: Keep `.agent_test_db/` directory after exit (default: cleanup)

**Exit Codes:**
- `0`: Success (limit reached and server exited cleanly)
- `1`: Build failure
- `2`: Server startup failed

**Behavior:**
- Uses `.agent_test_db/` directory for databases (automatically wiped and recreated)
- Runs on port 3290 by default
- Exits immediately when request or time limit is reached
- Cuts off in-flight requests on timeout
- Cleans up test databases automatically (use `--keep-db` to preserve)

**Usage Examples:**
- Test specific number of requests: `./agent_test_server.sh -N 50`
- Run for fixed duration: `./agent_test_server.sh -M 60`
- Combined limits: `./agent_test_server.sh -N 100 -M 120`
- With logging: `./agent_test_server.sh -N 25 -l test.log`
- Verbose debugging: `./agent_test_server.sh -N 10 -v`
- Keep databases for inspection: `./agent_test_server.sh -N 5 --keep-db`

### March Progress Testing

Use `tools/set_march_progress.sh` to set a character's march progress to an exact point for testing campaign/phase flows. It edits `game.db` directly via sqlite3 (no API calls), so the server can be running or stopped.

```bash
./tools/set_march_progress.sh                                          # List characters (find id)
./tools/set_march_progress.sh <target> <mini_game> <next_level>        # Set progress
./tools/set_march_progress.sh 12 tower_defense 5                       # Wolf marche, level 5 next
./tools/set_march_progress.sh player_one weeding 10                    # Wildlands marche, all 9 done
```

- `<target>`: numeric character id, username (exact), or display/safe name (substring). Ambiguous matches list matching rows and exit.
- `<mini_game>`: `tower_defense`/`wolf_marche`/`wolf` or `weeding`/`wildlands_marche`/`assarter`
- `<next_level>`: the next level the player will play, relative to the current marche; levels `1..next_level-1` of that marche are marked completed
- Track is derived from the character's current `game_phase`:
  - `initial_mission` → levels 1–9 (`next_level` 1–10; 10 = all 9 done, server auto-advances to `land_patent`)
  - `baron_track` → levels 10–25 (`next_level` 1–17 relative to the barony marche: 1 = its first level [absolute 10], 17 = all 16 done; levels 1–9 also filled)
- The script only touches `mini_game_progress` — never `game_phase`, sessions, or unlocks.

### Deployment Notes

1. **HTTP Only**: The server listens on HTTP (port 2290). TLS/HTTPS is handled by a reverse proxy (nginx, etc.)
2. **Databases**: `game.db` and `messages.db` are created in the working directory on first run
3. **Concurrent Access**: Two independent database connections allow simultaneous read/write operations
4. **Error Handling**: All errors return JSON with format `{ "error": "description" }`
5. **API Documentation**: Every endpoint has a corresponding `.md` file in `api/` directory with detailed request/response examples
6. **Server working directory**: The server binary runs from the `game/` directory. Image paths like `images/ui/` resolve relative to `game/`, so assets at `game/images/ui/book.png` are served from `/images/ui/book.png`. The Vite dev proxy (`/images` → `localhost:2290`) follows the same path resolution.

### Coding Standards

**Complete, Functional Code:**
- Code written MUST always be complete and functional
- Do NOT write stubs, placeholders, or "TODO: implement" code unless the user explicitly requests it
- When implementing a feature, ensure it works end-to-end
- If unsure about a design, ask the user rather than writing incomplete code
- Test your code before committing
- This ensures the codebase is always in a working state

**Naming Conventions:**
- Use snake_case for ALL names (variables, functions, classes, types)
- Examples:
  - `function_name()` not `functionName()`
  - `class_name` not `ClassName`
  - `struct_name` not `StructName`
  - `const session_token = ...` not `sessionToken`

**Code Organization:**
- Put logically separate chunks of code into their own functions
  - Unless this would require an absurd number of arguments
- Put closely related functions that are not closely related to other functions into their own files
- Use extremely descriptive variable and function names
- Do not use short names unless they are sufficiently descriptive
  - Exception: iteration variables like `i`, `j`, `k` are acceptable

**Memory Management:**
- Use RAII (Resource Acquisition Is Initialization) wherever possible
- Create RAII wrappers for memory management of code that doesn't natively support RAII
- Follow C++ standard best practices for resource management

**Filesystem Caching:**
- `std::filesystem::file_time_type` is broken on Linux — comparisons against a default-constructed value always produce incorrect results
- When comparing file modification times for cache invalidation, use POSIX `stat()` and `time_t` instead
  - Use `stat(path, &st)` to get `st.st_mtime` (a plain `time_t`)
  - Store the last-scan time as `time_t` initialized to `0`
  - Compare: `st.st_mtime <= last_scan_time_` for simple integer comparison
- The `TowerDefenseMapCache` (`server/TowerDefenseMapCache.cpp`) demonstrates the correct pattern

**Rationale:** These standards ensure:
- Consistent, readable code across codebase
- Automatic resource cleanup via RAII prevents leaks
- Logical code organization improves maintainability
- Descriptive names serve as inline documentation

## Client

The client is a Vite + Svelte 5 + TypeScript application that provides the game UI.

**Documentation:** See `client/README.md` for complete information about:
- Project structure and development workflow
- Game engine integration (simplegame)
- OPFS storage (Origin Private File System)
- Bootstrap 5.3.8 integration with dark mode
- Available storage functions and config storage API

### Coding Standards

**Complete, Functional Code:**
- Code written MUST always be complete and functional
- Do NOT write stubs, placeholders, or "TODO: implement" code unless the user explicitly requests it
- When implementing a feature, ensure it works end-to-end
- If unsure about a design, ask the user rather than writing incomplete code
- Test your code before committing
- This ensures the codebase is always in a working state

**Naming Conventions:**
- Use snake_case for ALL names (variables, functions, classes, types, interfaces)
- Examples:
  - `function_name()` not `functionName()`
  - `user_store` not `userStore`
  - `interface auth_response` not `AuthResponse`
  - `type api_options` not `ApiOptions`
  - `const session_token = ...` not `sessionToken`

**Styling - Bootstrap First:**
- All UI styling MUST use Bootstrap 5.3.8 classes (loaded via CDN in `index.html`)
- Custom CSS is ONLY permitted for styling that Bootstrap does not provide
- Bootstrap is required for all common UI patterns it supports:
  - Forms, inputs, buttons
  - Cards, alerts, modals
  - Grid, layout, spacing
  - Navigation, dropdowns
  - Loading spinners, badges
- Use Bootstrap's dark mode via `data-bs-theme="dark"` on `<html>` tag
- Reference: https://getbootstrap.com/docs/5.3/

**No Hardcoded User-Facing Text — Use the Text System:**
- ALL user-facing strings MUST come from the text system — never embed display text in Svelte markup, component `<script>`, error messages, or fallbacks
- Text lives in Markdown files at `game/text/<lang>/<id>.txt` (English is the source; other languages fall back to it)
- Fetch via `loadTexts(textIds)` / `loadText(textId)` from `client/src/lib/text.ts` (reads language + character stores, routes to the correct endpoint, caches). NEVER call `getTextsRequest`/`getCharacterTextsRequest` directly from components
- Render Markdown as HTML automatically with the `id` prop on `GameText`/`StoryText` (e.g. `<GameText id="td_ongoing_info" />`); use the `tokens` prop to substitute `{key}`/`<key>` placeholders
- Gender tokens (`{male|female}`) and `{character_name}` are substituted SERVER-SIDE by `getCharacterTexts` — the client never sends `sex` or the character name
- Adding a new string: (1) create `game/text/en/<id>.txt`, (2) fetch with `loadTexts` or render via a `GameText`/`StoryText` `id` prop
- Text IDs use lowercase snake_case; prefix UI strings with `ui_`

**SimpleGame (client/SimpleGame/):**
- **NEVER modify any file under `client/SimpleGame/`** under any circumstances
- SimpleGame is an imported engine project (local package via `file:./SimpleGame/ui` in package.json)
- If SimpleGame changes are needed for integration:
  1. Document the required changes in `docs/SimpleGame_changes.md` as a feature request to the engine project
  2. Only implement workarounds in our own code that avoid relying on the missing features
- Our integration uses SimpleGame's class definitions (`EnemyClass`, `GameObjectClass`, `ProjectileClass`, etc.) and collections (`gameObjects`, `enemies`, `projectiles`) as a library
- We do NOT call `initEngine()` — we run our own game loop via requestAnimationFrame

**TypeScript - Strict Typing:**
- Everything must have explicit types - no implicit or inferred types except in trivial cases
- All function parameters must have type annotations
- All function return values must have type annotations
- All variables must have type annotations when the type is not immediately obvious
- Use `interface` or `type` for complex data structures
- Use generic types where appropriate (`<T>`, `<K, V>`, etc.)

**The `any` Type:**
- `any` may ONLY be used when absolutely necessary
- When `any` is used, it MUST be documented with a comment explaining:
  - WHY it's necessary (what prevents using a proper type)
  - WHAT the expected structure is
  - WHY alternatives were not sufficient
- Example of properly documented `any`:
  ```typescript
  // any: Required because this config is loaded dynamically from server
  // Structure matches ApiResponse<T> but T is unknown at compile time
  const response: ApiResponse<any> = await fetchData();
  ```

**Code Organization:**
- Use extremely descriptive variable and function names
- Do not use short names unless they are sufficiently descriptive
  - Exception: iteration variables like `i`, `j`, `k` are acceptable

**Code Documentation:**
- All TypeScript functions MUST have JSDoc-style comments explaining:
  - What the function does (1-2 sentences)
  - All parameters with their types and purpose
  - Return value type and description
  - Usage notes or important behavior details
- All Svelte component functions MUST have comments explaining:
  - What the function does
  - Parameters and their purpose
  - Side effects or state changes
- Comments should be detailed enough that another developer can use the function without reading its implementation
- Example comment format:
  ```typescript
  /**
   * Attempts to automatically log in using stored credentials.
   * Checks OPFS for saved username/password, authenticates with server,
   * and restores user session state if credentials are valid.
   * Shows auth screen if no stored credentials or login fails.
   * 
   * @param none - Uses stored credentials from OPFS
   * @returns Promise<void> - Updates auth stores on success
   * 
   * Usage: Called from onMount when app initializes
   */
  async function attemptAutoLogin() {
    // ...
  }
  ```

**Rationale:** These standards ensure:
- Consistent, readable code across codebase
- Type safety catches bugs at compile time
- Explicit types serve as documentation
- Limited `any` usage prevents type safety erosion
- Code is self-documenting and maintainable

When working on the client, always consult `client/README.md` first for architectural context and usage patterns.

## API Documentation Files

Detailed API documentation is provided in the `api/` directory:
- Each API endpoint has its own corresponding `.md` file
- Complete request/response examples included
- Error codes and edge cases documented separately

## Config Linting

### `tools/check_configs.py`

Validates all JSON configuration files against their schema rules. Written in Python 3.13 with full static type annotations.

**Usage:**
```bash
./tools/check_configs.py              # Show errors and warnings
./tools/check_configs.py --no-warnings  # Show errors only
./tools/check_configs.py -h           # Show help
```

**Options:**
- `--no-warnings, -w`: Suppress warnings, only show errors
- `--config-dir, -c`: Directory containing config files (default: `game/config`)

**Exit Codes:**
- `0`: All configs valid (no errors found)
- `1`: Errors found (warnings return 0)

**What It Validates:**
- JSON syntax with helpful line numbers
- Required fields for each config type
- Field type validation (integers for levels, numbers for costs, etc.)
- Value range validation (max_level >= 1, positive speeds, etc.)
- Cross-reference validation (damage types must be defined in damage_types.json)
- Naming convention suggestions (lowercase snake_case IDs)
- Duplicate ID detection
- Missing required damage types (melee, ranged, magical)
- Money costs (`gold_cost` on buildings and walls) accept either a plain number (gold) or a `{ gold, shillings, pence }` object (non-negative, at least one key)
- Building `silver_pence_cost` must be a non-negative number array (penny-market cost, deducted from `fiefdoms.silver_pence`)
- Building `road_morale` must be an object with a positive `boost` and integer `distance >= 1` (road-network morale source)
- Building `road_tiles` must be a non-empty object of tile-key → image path; `road_tiles_canonical` a non-empty object of tile-key → `n`/`e`/`s`/`w` direction array (road auto-tiling)
- Building `water_source`/`water_powered` must be booleans; `pond_types` a non-empty array of `{id, capacity, max_level, ...}` pond type definitions; `race_tiles`/`race_tiles_canonical` follow the same tile-key → path / direction-array shape as `road_tiles` (channel auto-tiling)
- Building `arable_acres` must be a non-negative integer (optional; 0 = no arable land claimed)
- Building `forest_acres` must be a non-negative integer (optional; wood producers claim 80/60/70)
- Building `class` must be a lowercase snake_case string (optional; definitive grouping independent of `built_from`)
- Building `descriptions` must be an array of non-empty strings (optional; formal in-config design notes — never user-facing, stripped from client responses by the server)
- `economy.json` `starting_resources` must be an object of valid fiefdom resource keys → non-negative numbers
- `economy.json` `arable_land_by_level` must be an array of exactly 11 non-negative, non-decreasing numbers (index 0–10 for manor levels; 0 at level 0, 400 at level 1 → 1000 at level 10, following the 10-tier manor-house ledger — 400/465/530/600/670/740/800/870/935/1000)
- `economy.json` `forest_land_by_level` must be an array of exactly 11 non-negative, non-decreasing numbers (index 0–10 for manor levels; 0 at level 0, 200 at level 1, 600 at level 10)
- Combatant `requires_manor_level` must be a positive integer (optional; gates which classes appear on the recruit market)
- Building `recovery_multiplier` must be a non-negative number (optional; infirmary healing-rate bonus), `tech_trees` an array of non-empty strings, `tech_xp_per_day` a non-empty non-negative int array, `max_concurrent_orders` a positive integer

**Config Files Validated:**
- `game/config/damage_types.json` - Damage type definitions
- `game/config/player_combatants.json` - Player unit definitions
- `game/config/enemy_combatants.json` - Enemy unit definitions
- `game/config/fiefdom_building_types.json` - Building type definitions
- `game/config/heroes.json` - Hero definitions with equipment, skills, and status effects
- `game/config/fiefdom_officials.json` - Fiefdom official templates with stats and roles
- `game/config/manor_ui.json` - Manor UI config (`build_order` governs the build-palette button order only — it never overrides the client's display/level/affordability filters)
- `game/config/manor_river.json` - River templates (meandering polylines `points` + band `width`; seeded per-fiefdom with 0/90/180/270° rotation — see `server/tables/fiefdom_river.md`)
- `game/config/retinue.json` - Retinue tuning: `retinue_capacity_by_level` (11 entries, manor levels 0-10; the knight is free), market params (candidates per class, max candidate level offset, grace buckets), recovery (max hours, min-deploy HP), morale contributors, sell discount, `armory_capacity`/`storage_capacity`, training (teacher/book XP grants + durations)
- `game/config/equipment.json` - Gear items: slots, member-level requirements, per-item daily upkeep, `armory_slots`, `base_value`, `city_price`, and optional `craft` (materials + duration + `tech_node`)
- `game/config/items.json` - General-storage items (books etc.): `source` (`drop`/`purchase`), `xp_grant`, `training_duration_hours`
- `game/config/tech_trees.json` - Technology trees: nodes with `xp_cost`, `prerequisites`, and `effects` (start: `unlock_recipe`)
- `game/config/analyzer_manor_strategies.json` - **Analyzer-only** config: weighted manor build policies (heuristics) with optional min-ratio constraints, consumed by `game_balance_analyzer --sim`. Not a game config — see the `analyzer_` prefix convention below.

**`analyzer_` prefix convention:** Config files used **only** by the `game_balance_analyzer` tool (never by the game/server) live in `game/config/` and MUST be prefixed with `analyzer_` (e.g. `analyzer_manor_strategies.json`) so they are not confused with real game config. Any future analyzer-only config follows the same convention and is added to this list + validated in `tools/check_configs.py`.

**Image Directory Validation:**
- `game/images/` - Game images (auto-detected from directory structure; only `combatants/`, `buildings/`, `heroes/`, `portraits/` entity directories are validated)
- Linter validates: required directories exist and are non-empty, file naming convention
- See README.md "Images" section for directory structure specification

## Config File Changes Rule

**When any JSON config file is modified:**

1. Run `./tools/check_configs.py` to verify validity before committing
2. If errors exist, fix them before proceeding
3. Update the corresponding documentation in `server/docs/`:
   - `fiefdom_building_types.json` → `server/docs/fiefdom_building_types.md`
   - `damage_types.json` or combatant files → `server/docs/combat_system.md`
   - `heroes.json` → `server/docs/heroes.md`
   - `fiefdom_officials.json` → `server/docs/fiefdom_officials.md`
4. Update AGENTS.md if the change affects config structure or validation rules
5. **Tower defense maps:** If adding or modifying tower defense maps:
   - Map metadata JSON files go in `config/tower_defense/maps/`
   - Map background images go in `images/tower_defense/maps/`
   - Map files follow the format in `tower_defense_map_metadata_format.md`
   - `mini_games.json` level entries reference maps by filename (e.g. `"map": "td_level_1.json"`)
   - Maps are loaded dynamically at runtime — no server restart needed
6. **Image directory updates required:** If adding new combatants, buildings, heroes, or officials:
   - Combatants: Create `images/combatants/{id}/idle/`, `attack/`, `defend/`, `die/` subdirectories
   - Buildings: Create `images/buildings/{id}/construction/`, `idle/` subdirectories
   - Heroes: Create `images/heroes/{id}/idle/`, `attack/` subdirectories and `skills/{skill_id}/` for icons
   - Officials: Create `images/portraits/{portrait_id}/` directories with portrait images
   - Add at least one image file (1.png, etc.) to each required subdirectory
   - Linter will warn if required directories are missing or empty

This ensures:
- Config files remain syntactically valid
- Documentation stays synchronized with actual config structure
- The linter accurately reflects validation requirements
- Images directory matches config changes

## Current Session Context

### Goal
Build the full game progression and content system with a working tower defense minigame and polished gameplay loop.

### Progress

#### Done
- **Multi-round system**: Each level in `mini_games.json` has a configurable `rounds` field (3-8 per level). Server tracks current_round in game_sessions. On win, if more rounds remain, server returns `next_round: true` with fresh `spawn_schedule` instead of ending the game. Client shows "Round N/M" indicator, clears enemies between rounds, and offers "Next Round" button or auto-advance after 2s delay.
- Created `text/` directory with translation system
- Created `game/config/` with tower defense configs (mobs, towers, units, projectiles, maps)
- Built server with auth, game sessions, TD round lifecycle
- Built client with Svelte 5 + SimpleGame engine integration
- **SimpleGame collision system**: Projectiles now use `onCollisionWithEnemy()` callbacks instead of manual `dist < 12` — engine's quadtree handles bounding-box overlap detection
- **Projectile config**: New `game/config/tower_defense/projectiles.json` with width, height, speed (1600 = 4×), image_file, forward_vector. Server sends it in TD kickoff response
- **Engine movement**: Projectiles move via `doMovement()` using `direction_x/direction_y` + `velocity` (set by `setOrientationTowards()`). `projTick()` only updates homing direction
- **Mob sprite mirroring**: `mirrorOnDirection = true` + `forwardVector` from config on all mobs
- **Soldier sizes**: Bumped +50% (archers 48×48, others 54×54)
- **NaN destination fix**: Removed `rename_field(wp, "x", "x")` self-rename that destroyed waypoint x coordinates
- **Pause button**: Added sidebar pause button using SimpleGame's exported `togglePause()`/`isPaused()`/`onPause`/`onResume` API
- **Forfeit fix**: `forfeitGame()` skips server completion call if round never started
- All `console.debug` → `console.log` for visible debug output
- **Barony honor naming**: Capturing the aspiring barony's honor name up front. `player_game_state.honor_name` (new column + migration) is captured by `/api/startBaronTrack` (required, ≤ 64 chars, **hard** case-insensitive uniqueness against `baronies.name` — not a warning), prefilled (editable) into `/api/createBarony`, substituted for `{honor_name}` server-side in `getCharacterTexts`, and shown on `barony_patent_earned.txt` via a `DialogOverlay` on `baron_right_earned` before the create form. Name dialog lives in `LandPatentPanel` (text-system strings); `createBarony` duplicate check now `LOWER(name)=LOWER(?)`.
- **Penny market + peasant economics**: Peasant Cottage produces **74 grain/day** and costs **36 grain/day** upkeep (net +38/day, auto-sold above reserve). Grain is a **penny-market** resource: `import_prices.grain = {"shillings": 1}`, so imports deduct from `fiefdoms.silver_pence` and excess auto-sells to it at 6 pence/unit (50%). Engine now loads/uses `silver_pence` in the economy tick (imports+exports+persist), reports `net_silver` and `{amount, pence}` export entries. Currency switched to standard medieval: **12 pence/shillng, 20 shillings/pound, 240 pence/gold** (`economy.json`, `Money.hpp`, mini-game reward formatting). Client shows the silver balance + pence-aware export/reserve labels.
- **Per-commodity export pricing**: Export price resolves per resource with precedence `export_prices[resource]` (explicit gold/pence sell price) → `export_sell_multipliers[resource]` (ratio of import price) → global `export_sell_multiplier` (0.5). Engine resolves the unit sell value in the resource's market currency; linter validates both new maps in `economy.json`.
- **Multi-output buildings + per-output rates**: A building may define an `outputs` array — each output with its own `inputs`, a `min_level` unlock, and a per-player rate (0..1). The blacksmith now produces `ironwork` (level 1+) and `fancy_ironwork` (level 2+, 2× iron input), both simultaneously at level 2+. Engine uses per-output plans: each output is gated by its own input-satisfaction ratio, rates scale output + inputs (0 = off). `fancy_ironwork` added as a real fiefdom resource (column + migration + full plumbing). Rates stored in `fiefdom_buildings.output_rates` (JSON), set via `/api/setBuildingOutputRate`; client has a Production Rates panel in `ManorMenu` with per-output sliders.
- **Metalworking economy + household grain**: Every craft building (peasant, blacksmith, collier, woodcutter, wood_hewer) is a self-contained household — produces **18 grain/day** and consumes **36 grain/day** via `daily_cost`, plus a small `ironwork` tool upkeep (home_base 20, peasant 1, woodcutter 5, wood_hewer 5, miller 5, collier 2). Blacksmith output scaled to **100 ironwork/day** (input-gated on charcoal 100 + iron 40 per day; no separate charcoal `daily_cost`, so one collier's 120 charcoal nets ~+20 vs one blacksmith); collier charcoal 120 (wood 80 input), bloomery iron 20 (charcoal 30 input), woodcutter wood 20 — so ~5 bloomeries + ~9 colliers feed one full blacksmith, and one blacksmith covers ~50 peasants + 2 woodcutters + 2 woodhewers + a miller + a few colliers + the home base. **Prices deflate with production** (anchored to grain at 1 shilling ≈ 0.05 gold): wood 0.03, charcoal 0.03, iron 0.06, ironwork 0.02 import. **Ironwork exports sell at 25% of import** (`export_sell_multipliers.ironwork = 0.25`), other resources at 50%. `default_reserves` scaled up (grain 150, wood 100, steel 50, bronze 25, leather 25, mana 10, charcoal/iron/ironwork 50, fancy_ironwork 10).
- **Realtime combat scaffold**: Server-authoritative RTS over WebSocket `/ws/combat` (same port 2290) — matches live entirely in RAM (`server/combat/`), one worker thread per battle from a bounded pool (`--combat-sim-threads`), 10 ticks/s with entity-level deltas + full snapshots every 25 ticks, pluggable codec (`combat_codec`; `json_codec` default with hand-rolled fast serializer, `binary_codec` placeholder), uWS pub/sub topics for broadcast (`match:<id>`, team chat, per-player voice signaling), SQLite writes deferred to the loop thread (`Loop::defer`). REST surface: `combatCreate`/`combatJoin`/`combatList`/`combatGetConfigs`/`combatMatchmaking` (stub)/`getRetinue`. **Retinue defined now**: new `retinue_members` table (knight = character, auto-created; units come from the manor's `train_troops` stub later), casualties persisted after matches per ruleset death handling. Configs: `combat/rulesets.json` (skirmish PvE **wounded**/no-death + scrimmage PvP respawn) and `combat/maps/meadow.json` (16×16 with normalized spawn points, tile_costs cost grid for future pathfinding, lenient parse — unknown fields preserved, linter warns not errors). Client: `src/combat/` — CombatScreen (lobby→battle→results), CombatNetClient (first-message auth, backoff reconnect), CombatGame (SimpleGame canvas, entity store + interpolation, select/move), CombatHud, CombatChat (team), CombatVoice (WebRTC mesh, WS-relayed signaling, STUN placeholder), MatchLobby (create/join by code). No hub cards — combat is reached through game flow later (module kept for future integration). Vite proxies `/ws` (ws:true); nginx needs Upgrade headers + long timeouts (server README).

- **Hash-based history routing**: In-app navigation is URL-driven via `client/src/lib/router.ts` — `#/` hub, `#/activity/<id>[/…]` (arbitrary-depth nested sub-routes, e.g. `#/activity/chat/thread/42`), `#/game/<game>/<level>`. Components read `route_store` and call `navigate()` / `replace_route()` (redirects: game complete/error replaces the game entry so Back doesn't re-enter it) / `go_back()` (in-app Back buttons). Entering an activity/game from an empty/foreign URL pushes a `#/` hub entry first so browser Back always lands on the hub grid. Reload restores the screen from the URL; mobile app-switch fires no hash events and never disturbs the open screen. The old OPFS `last_activity` restore is gone; barony create/join → hub, baron-track start → `#/activity/tasks`. Backing out of a mini-game mid-round leaves an active session that the server resumes on next kickoff (existing logic).
- **Manor loading fixed**: Two bugs kept the manor at an infinite "Loading Manor…" spinner. (1) `/api/getBuildingConfigs` is authenticated; the client now sends `auth` (it previously called without credentials, got a `needs_auth` response with no `error`/`data`, silently returned `undefined`, and `Object.entries(undefined)` in `ManorMenu.setupGame` threw). (2) `whenLoaded(setupGame)` was registered *after* `initEngine(canvasEl, debugDiv, false, () => {})` — initEngine's synchronous first loop closes the one-shot "all classes loaded" gate, so a late-registered `whenLoaded` never fires and `loading` stays true. Fixed by passing `setupGame` as initEngine's 4th argument (the documented pattern in SimpleGame/Embedding.md, matching TowerDefense/WeedingGame/CombatGame) and removing the `whenLoaded` call. The server now also serves `/images/manor/*` (background + building sprites). `getBuildingConfigsRequest` takes `{ username, token }` and throws on missing data; the manor loading spinner has a Back button as an escape hatch.
- **Manor auto-build + construction**: On first entry, `ManorMenu.initialize` checks the fiefdom state and, if no `home_base` building exists, auto-places one at (0,0) — free (config `*_cost[0] = 0`), with `construction_start_ts` set to now so the construction timer starts on first entry and the server auto-levels to 1 after `construction_times[0]` on the next fiefdom time-update. `/api/Build` requires `character_id` (ownership check against `fiefdoms.owner_id`); `buildRequest` sends `$currentCharacter.id` from both the auto-place and toolbar `placeBuilding`. Construction progress bars compute live time via the `setProgressBar` getter (re-evaluated every frame), so the manor house visibly builds over 60s. Auto-place failures are `console.log`-ed (not silent).
- **Manor roads**: `road` is a 1×1 building type (max_level 1, instant build via `construction_times[0] = 0`, costs 1 silver pence via `silver_pence_cost`). Roads auto-tile: the client picks a base image from `road_tiles` using the connectivity masks in `road_tiles_canonical` (n/e/s/w per side) and rotates it with `setOrientation` (procedural canvas tiles in `ManorMenu.svelte`; `tools/generate_road_tiles.py` can emit PNGs for real art later). Road-network morale: buildings with `road_morale` (`boost` + `distance`) radiate points along orthogonally-connected road tiles (BFS in `Morale::computeRoadMoralePoints`); each building touching an in-range road tile gets `boost` points, stacking additively (a source never boosts itself), and the economy tick multiplies that building's outputs by `1 + points × economy.json.morale_production_multiplier` (default 0.02). `getFiefdom` returns `road_morale` (building_id → points) when buildings are included; the Production panel shows the resulting "+X% production". Silver-pence build costs are deducted/refunded through the full build/demolish/move/upgrade cost paths.
- **Water power scaffold**: Per-fiefdom rivers (lazy-seeded from `manor_river.json` templates — meandering polylines in a corner band, rotated 0/90/180/270° deterministically by fiefdom id; stored in `fiefdom_river`), the 8×8 **mill_pond** (water_source; `pond_types` earthen 2 / timber 4 / stone 6 capacity, each with levels; type upgrade via new `/api/Build` action `upgrade_pond_type`), **head_race** (wooden, 2w + 2d, 10s build) and **tail_race** (1d, instant) 1×1 auto-tiling connectors (`race_tiles`/`race_tiles_canonical`), the `water_powered` building flag + a sample **mill** (grain output gated on power). `Water::computeWaterPower` (new `server/WaterNetwork.cpp`): active pond = footprint touches river; head-reach BFS over head-race cells from the pond; tail-reach BFS over tail-race cells from the river; powered = drained ∧ fed by a pond with spare capacity (greedy by building id). Economy tick skips outputs of unpowered water buildings (daily_cost still applies); `getFiefdom` returns `river_cells`, `water_power`, `water_power_detail` (powered_by + pond_load). Buildings can't overlap river cells (build-time rejection). Pond cost/construction arrays resolve from `pond_types[pond_type]` (new `Validation::getBuildingArrayField`/`getNextLevelCost`/`getBuildingMaxLevel` used by upgrade, construction-completion, refund, and cumulative-cost paths — this also **fixes a latent bug** where `/api/Build` upgrade costs used `gold_cost`-style keys instead of resource names, so upgrades effectively never charged/deducted). Client: procedural water tiles + race auto-tiling (generalized channel tile helper), river-overlap rejection in the placement ghost, green/red powered dot on water-powered buildings, a `Type · load/capacity` label on ponds, and construction progress bars that resolve per-pond-type `construction_times` (`getConstructionTimes` in `ManorMenu.svelte`).
- **Manor rendering fixed**: The manor now uses SimpleGame's newer `setViewportSize`/`setCameraPosition` (canvas = displayed size → no aspect distortion; camera centered on the board = manor house). `g2b`/`getRect` were changed so a building's **center** sits on its cell — `home_base` at cell (0,0) is exactly at the board center (was corner-at-center). The world-space `resText`/`mlText` HUD text was removed; the **Main** button (returns to hub; text id `manor_main_btn`), **Build** toolbar toggle (text id `ui_manor_build_btn`), and the **Economy**/**Production** panel toggles (text ids `ui_manor_economy`/`ui_manor_production`) are all SimpleGame **HUD** objects (`obj.hud = true`, screen-space, `simplegame.ts` commit 9df44fc) fixed to the viewport regardless of panning. All four live in one top-right `Column` (`actionCol`, 140×40 each, stack order Economy → Production → Build → Main), styled with the engine's newer button APIs to match Bootstrap `btn-outline-light` but with a **semi-opaque** fill — `color '#212529'` at `backgroundOpacity 0.55`, `foregroundColor '#f8f9fa'`, `cornerRadius 6`. The Economy/Production buttons highlight to Bootstrap primary `#0d6efd` while their panel is open (`setBackgroundColor`, which re-derives hover/click). The panel cards anchor below the stack via a `panelTop` bound to `apply_viewport()`. The build column is **hidden by default** and revealed by the Build toggle; its buttons are 280×56 with the icon **left** of the text, sized to a **fixed height of 32px preserving aspect ratio** (`setAspectIconSize` reads `naturalWidth`/`naturalHeight` — building art is not square), same glassy style, and labels come from the text system (`ui_building_<type_id>`, e.g. `ui_building_blacksmith`) with the **level-1 cost appended** (compact `10s 10w` via `formatCost`: gold in shillings + non-zero pence via `formatShillingsPence`, e.g. `12s`/`30s 6d` — never fractional gold; wood→`w`). Buttons **grey out via `setDisabled`** when the building can't be built — `canBuild` mirrors the server: manor level (level-locked types are hidden from the palette entirely), `max_per_fiefdom` reached, level-1 `prerequisites[0]` non-`manor_level` keys unmet (e.g. `wood_hewer`/`collier` require a level-1 `woodcutter`), or costs unaffordable against the fiefdom's current resources. States refresh via `updateBuildButtonStates()` on every `loadFiefdomData`. The `house` type (no `display_name`/`image`) is excluded by the `display_name && image` guard in the `buildableIds` filter. Placement toggles via re-clicking the building button or Esc; the ghost places on a valid square via **click** or **drag-and-release** (`ghostBuilding.onClick(0, …)` + `onDragEnd(0, …)`, gated on `ghostValid` — never on mid-drag `onDragMap` moves); the loading/intro/error panels are absolute overlays so the canvas is always measurable. Board panning stays enabled; the viewport re-applies on window resize without re-centering (panned position survives). **Palette button order is config-driven** via `game/config/manor_ui.json` `build_order` (injected by `/api/getBuildingConfigs`; the client sorts `buildableIds` by it, unlisted ids sort last) — it governs order only and never overrides the display/disable logic; reordering is a one-file edit.

- **Arable land**: An abstract, un-rendered resource limiting manor growth (no DB column — derived from `manor_level` + the building inventory). The manor house's `max_level` is now **10** (was 32) and `manor_level` = home_base level, so `manor_level` ranges **0–10**; new fiefdoms default to 0 (home_base under construction). Total acres = `economy.json` `arable_land_by_level[manor_level]` (400 at level 1 → 1000 at level 10, pretty-round linear). Each building optionally claims `arable_acres` (15 peasant/villein, 30 freeholder/yeoman, 7 for the 18-grain craft households, 0 for infra/industrial/modifier). `/api/Build` and stage **convert** gate on `available ≥ claimed` (`insufficient_arable_land`), converting only on the delta; demolishing frees acres. Server: `Validation::getUsedArableAcres`/`getTotalArableAcres`/`getAvailableArableAcres` (ActionHandlers.cpp), `GameConfigCache::getBuildingArableAcres`/`getArableLandByLevel`. `/api/getFiefdom` returns `arable_land: { total, used, available }`; `/api/getBuildingConfigs` injects `arable_acres`. Client: `canBuild`/`checkVal` mirror the gate and the Economy panel shows "Arable land: used/total" (`ui_manor_arable`). Linter validates `arable_acres` (non-negative int) and `arable_land_by_level` (11-entry non-decreasing array). Analyzer: `building_type::arable_acres`, `manor_economy::arable_land_for_level`, the sim hard-gates `is_feasible` on acres (400 at level 1), and the network report emits `arable_acres`/`arable_total` + a warning when exceeding the level-10 max.
- **Money fungibility + build auto-import**: `gold` and `silver_pence` are now fungible (1 gold = 240 pence, the `currency` block) everywhere — a penny-market cost/import can be paid with gold and vice versa. Fixed a bootstrap deadlock where a new fiefdom's 5 gold (= 1200d) couldn't pay the first peasant's penny-market materials (wood/ironwork/beams/boards ≈ 247d) because gold and silver were treated as separate wallets, so the sim built nothing. Build/upgrade/convert now **auto-import material shortfalls** at import price with fungible money (respecting per-resource `import_settings`), and the economy tick's penny-market imports can draw on gold. Server: `hasEnoughResources`/`deductResources` (now `GameConfigCache&`-taking, import-aware + fungible, all ~12 callers updated) and `game_logic.cpp` `supply_need`. Analyzer `--sim`: `is_feasible` (gold-equivalent demand vs `total_gold_equivalent`), `commit_build` (`spend_gold`/`spend_silver` with cross-currency conversion), and `manor_economy.cpp` `supply_need` (silver+gold×240 pool). Verified: sim now builds (arable-capped land-using buildings hit 397/400 acres; 0-arable modifier/industrial buildings like miller/bloomery with no `max_per_fiefdom` build unboundedly, a separate pre-existing characteristic).
- **Fiefdom building overhaul**: every type now carries a **`class`** string (definitive grouping — `peasant` = villein/freeholder/yeoman, `flourmill` = miller/windmill/watermill, etc.); modifiers/prerequisites match a class in addition to the `built_from` chain (`counts_as` in game_logic.cpp, `buildingSatisfiesRequirement`, analyzer `satisfies`). The `peasant` type was **renamed to `villein`** (old duplicate `villein` deleted). Stage chains now gate **stage 2 = manor 3, stage 3 = manor 6** (water-powered stage 3s keep `mill_pond:1`). Flourmills: miller = flour_milling ×1.1/max100 + grain daily_cost 144 (36 family + 108 horse); windmill ×1.3/max200, watermill (`mill`) ×1.3/max300, both grain daily_cost 36; the watermill is now a flour_milling modifier (was a standalone 30-grain producer) and its modifier is **water-power gated** (`computeBuildingModifiers` now takes `water_powered_ok`). Woodchain normalized to +20%/stage (woodcutter 20 → coppicer 24 → timber_hauler 28.8); blacksmith chain ironwork normalized (100 → 120 → 144, inputs scaled with outputs). **Forest land** added as an off-map resource analogous to arable: `economy.json` `forest_land_by_level` (200→600), `forest_acres` on woodcutter 80 / coppicer 60 / timber_hauler 70, server gate `insufficient_forest_land` (build + convert delta), `getFiefdom` `forest_land`, `getBuildingConfigs` injects `forest_acres`/`class`, client gate + Economy panel (`ui_manor_forest`), linter validation, analyzer forest gate + network `forest_acres`/`forest_total`. **Sim now enforces `manor_level` requirements and can upgrade the manor** as a weighted action (`upgrade_manor`, weight 1.0 default / MC random / heuristic `weights["upgrade_manor"]`): `is_feasible` takes `manor_level`, land totals scale with the current manor level, and the trace shows `[ml=N]` per day. **Analyzer modifier model matches the server's `modifier_id` non-stacking rule**: `manor_economy` groups modifier sources by `modifier_id` (each target boosted at most once per group, strongest source — level then multiplier — covers the population), and `network.cpp`'s `flows_for` does the same in its aggregate model (slot-weighted mean over covered targets) — so a miller and windmill never stack on the same grain output in the sim or the network report.

- **Manor-house ledger + stone removal + `descriptions`**: The manor house (`home_base`) upgrade costs now follow the 10-tier ledger (`~/Downloads/manor_house_upgrade_costs.md`): level-indexed arrays resized 14 → 10 entries; build (level 1) is **free** (a gift from the lord's family — the ledger's "Loaned Carpenter + Grant"); levels 2–10 charge the ledger's guild-contract gold (`gold_cost` 0.5/1.75/28/44/130/275/490/820/1350), `wood_cost` (cords), `beams_cost`/`boards_cost`, `iron_cost` (blooms), and `ironwork_cost` (forged ironwork) per tier. **`stone_cost` is removed from the game**: deleted from all 13 stone-cost buildings, the mill-pond top level + its three `pond_types` (earthen/timber/stone — type names kept as physical descriptors), and `wall_config.json`; `stone` dropped from `economy.json` `import_prices`/`default_reserves`/`starting_resources` and from both mini-game `completion_bonus` grants; the linter's resource/cost sets, client import/label/format maps, and analyzer resource lists no longer reference it. The DB `stone` column + C++ plumbing are **retired** (stays 0) — config-driven removal, zero server economy changes. `construction_times` trimmed to 10 entries; upgrade **tenant gates** (`dependencies`) now require the manor ~⅔ full of villeins (level 2 = 15 → level 10 = 34; target renamed `peasant` → `villein`). `arable_land_by_level` follows the ledger's Arable Farmland column: 400/465/530/600/670/740/800/870/935/1000.
- **`descriptions` internal-note field**: every building type may carry a `descriptions` array — formal in-config design notes (JSON has no comments). Linter validates it (array of non-empty strings); the server **strips it** from `/api/getBuildingConfigs` and the `getGameInfo` `fiefdom_building_types` branch so the notes never reach the client. `home_base` carries the full ledger rationale; the stone-removal buildings carry one-line notes.

#### In Progress
- (none)

#### Blocked
- (none)

### Key Decisions
- **SimpleGame collision system**: Using `onCollisionWithEnemy()` for projectile-enemy hit detection. Fires every tick bounding boxes overlap, but `p.destroy()` in the callback prevents re-triggering. This is more accurate than manual `dist < 12` and follows engine patterns.
- **Engine movement for projectiles**: `doMovement()` handles position updates via `direction_x/direction_y * velocity * delta_t`. `projTick()` only updates homing direction each frame (1-frame lag, standard).
- **Speed from config**: Projectile speed in `projectiles.json`, set via `p.setSpeed(cfg.speed)` at spawn. No hardcoded 400 anymore.
- **`setOrientationTowards()`** is used for both visual rotation AND movement direction — it sets `orientation` (for `ctx.rotate`) and `direction_x/direction_y` (for `doMovement`).
- **`rename_field` self-rename bug**: `rename_field(wp, "x", "x")` with `from == to` destroys the field (self-move + erase). Solution: just delete these no-op calls.
- **Sprite mirroring**: SimpleGame applies `ctx.scale(-1, 1)` at render time when dot product of movement direction and `forwardVector` is negative. Mob sprites should face RIGHT in source images (forward_vector: [1, 0]).

### Next Steps
- Add more map JSONs for levels 2–9 in `mini_games.json`
- Fill in narrative placeholder text in `text/en/` with gender-substituted content
- Create arrow/bolt projectile images at `game/images/tower_defense/projectiles/`
- Generate parchment background and path icon images
- Add `rounds` field to baron_levels in `mini_games.json` if needed
- Combat: implement real combat mechanics (damage, target selection, abilities) — currently attack/ability relay as events only
- Combat: PvE enemy AI (flow-field pathfinding over `tile_costs`; enemies currently idle)
- Combat: PvP matchmaking (challenges/acceptances/handicaps); per-user team selection
- Combat: barony-invite UI via the messaging system; `combatList` browser
- Combat: TURN relay for voice chat; WebRTC peers on late-joiners; per-player fog of war
- Combat: `train_troops` manor integration (units with weapons/armor/abilities from `player_combatants.json`/`heroes.json`)

### Critical Context
- **Server compiles** with `./compile_server.sh` (no arguments). Client `npm run check` passes with only pre-existing WeedingGame errors.
- **SimpleGame is never modified** — all changes in our code (`TowerDefense.svelte`). Required changes documented in `docs/SimpleGame_changes.md`.
- **Tower defense maps** live in `game/config/tower_defense/maps/` as JSON. Only `map_1.json` exists (level 1). Map metadata normalized server-side from camelCase to snake_case.
- **`TowerDefenseMapCache`** uses POSIX `stat()` with `time_t` (not `std::filesystem::file_time_type` — broken on this Linux).
- **`enemies.delete(e)` required manually** — SimpleGame's `EnemyClass.destroy()` doesn't remove from the global `enemies` set, only from `gameObjects`.
- **Projectile collision flow**: `combatTick()` (spawn) → `doMovement()` (engine moves) → `doCollisionDetection()` (engine detects overlap, fires callback) → `everyTick()` (our `projTick` updates homing direction).
- **Projectile images** referenced at `/images/tower_defense/projectiles/hunting_arrow.png` and `war_arrow.png` — these files don't exist on disk yet.
- **Login debug**: Conditional on `static const bool login_debug = false` in main.cpp (off by default).
- **`rename_field` self-rename bug**: `rename_field(obj, "x", "x")` with `from == to` destroys the field. Fixed by removing these calls.
- **Level ID** threaded through: `App.svelte → MiniGameContainer → TowerDefense` (not hardcoded 0).
- **Combat is not a mini-game**: it never routes through `MiniGameContainer`/`startMiniGame`; the `client/src/combat/` module is kept for future game-flow entry (hub cards were removed — combat is no longer launched from `available_activities`).
- **Combat threading**: match sim runs on its own worker thread; the uWS loop thread only enqueues commands and relays chat/voice; SQLite writes are `Loop::defer`-ed back to the loop thread.
- **Combat maps are lenient**: `CombatMapCache` preserves unknown fields; the linter warns (not errors) on them so the format can evolve (see `docs/combat_maps.md`).

### Relevant Files
- `game/config/tower_defense/projectiles.json` - Projectile config (width, height, speed, image_file, forward_vector)
- `game/config/tower_defense/mobs.json` - Mob config with forward_vector per mob
- `game/config/tower_defense/units.json` - Soldier/unit config with width/height
- `game/config/tower_defense/towers.json` - Tower config
- `game/config/tower_defense/maps/map_1.json` - Only map file (level 1)
- `game/config/mini_games.json` - Level grids, map references
- `server/GameConfigCache.hpp/.cpp` - Config loading, includes projectiles
- `server/main.cpp` - All API handlers, TD kickoff sends projectiles config
- `server/TowerDefenseMapCache.cpp` - Map loading/normalization (fixed self-rename bug)
- `client/src/minigames/tower_defense/TowerDefense.svelte` - Main TD game: engine collision, homing, mirroring, projTick cleanup
- `client/SimpleGame/Embedding.md` - SimpleGame API docs (pause, mirroring, collision, movement)
- `game/config/combat/rulesets.json` - Combat mission rules (skirmish PvE / scrimmage PvP)
- `game/config/combat/maps/meadow.json` - First combat map (tile_costs grid + spawn points)
- `server/combat/` - CombatTypes, CombatCodec (json/binary), CombatMatch, CombatMatchManager, CombatMapCache
- `server/RetinueDB.hpp/.cpp` - Retinue persistence (knight auto-create, casualties)
- `client/src/combat/` - CombatScreen, CombatNetClient, protocol.ts, CombatGame, CombatHud, CombatChat, CombatVoice, MatchLobby
- `docs/combat_protocol.md` - WS protocol contract (envelope, messages, replication, topics)
- `docs/combat_rulesets.md` / `docs/combat_maps.md` - Ruleset + map format specs
- `server/tables/retinue_members.md` - Retinue table schema
- `server/WaterNetwork.hpp/.cpp` - `Water::computeWaterPower` (pond activation, head/tail reach, capacity gating)
- `game/config/manor_river.json` - River templates (meandering polyline `points` + band `width`)
- `client/src/components/ManorMenu.svelte` - Manor board: roads, water power, construction, HUD, panels, building info card (upgrade/convert/demolish/pond-type)
- `server/tables/fiefdom_river.md` - River table schema (lazy seeding, rotation)
- `game/config/fiefdom_building_types.json` - Building types incl. stage chains (`built_from`), level-indexed production arrays, production-resource build costs
- `server/ActionHandlers.cpp` - Convert action, chain-aware counting, cost-system extension (charcoal/iron/ironwork/fancy_ironwork)
- `server/docs/fiefdom_building_types.md` - Stage chains, level-scaled production, money-object amounts