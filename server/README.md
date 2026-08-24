# Ravenest Server

C++23 HTTP API server for Ravenest Build and Battle game backend.

## Overview

High-performance server built with:
- **uWebSockets**: Async HTTP endpoint handling
- **nlohmann/json**: Modern JSON parsing
- **sqlite_modern_cpp**: SQLite ORM wrapper
- **OpenSSL**: SHA-256 for token generation
- **glibc crypt()**: Yescrypt ($y$ prefix) for password hashing

Design goals:
- Minimal code duplication via unified handler
- Pre-dispatch authentication for all endpoints
- Clean architecture with logical header separation

## Architecture

### Request Flow

```
Request → POST /api/{endpoint}
    ↓
Unified /api/* handler (single handler for all endpoints)
    ↓
[1] Buffer accumulation (once)
    ↓
[2] Parse JSON body
    ↓
[3] Extract endpoint name from URL (/api/login → "login")
    ↓
[4] Extract auth object and IP address (X-Real-IP header)
    ↓
[5] CALL handleAuth() - authentication logic
    ├─ Returns AuthResult with:
    │   - authenticated username (optional)
    │   - new_token (optional)
    │   - needs_auth (bool)
    │   - auth_failed (bool)
    │   - error (optional)
    │
    ├─ If auth fails/needs auth: return early with ApiResponse
    └─ If auth succeeds: proceed to dispatch
    ↓
[6] Endpoint dispatch
    ├─ If endpoint == "createAccount": handleCreateAccount() (special case)
    └─ Otherwise: lookup in handler map → dispatch to handler function
    ↓
[7] Handler returns ApiResponse
    ↓
[8] sendJsonResponse(ApiResponse)
```

### Header File Organization

```
server/
├── main.cpp              # Entry point, unified handler, all endpoint implementations
├── init_db.cpp            # Database initialization (tables + indexes)
├── init_db.hpp            # Database initialization declarations
├── CMakeLists.txt        # Build configuration (FetchContent for deps)
├── Database.hpp          # Database singleton & connection management
├── ApiResponse.hpp       # Response structure with auth flags
├── AuthManager.hpp       # Token cache and SHA256 generation
├── ApiHandlers.hpp       # Handler types, ClientInfo, endpoint map
├── PasswordHash.hpp      # glibc crypt() yescrypt hashing
├── SafeNameGenerator.*   # Safe display name generation
├── GameConfigCache.*     # Game configuration JSON caching
├── images/
│   ├── ImageCache.hpp    # Image directory scanning and caching
│   └── ImageCache.cpp    # Image path queries and filtering
```

## Authentication System

### Request Format

All endpoints expect (except createAccount):
```json
{
  "auth": {
    "username": "player_name",
    "password": "xxx"                    // OR
    "token": "yyy"
  },
  "player_id": 123                         // endpoint-specific fields
}
```

### Token Generation

**Algorithm**: `SHA256(secret_salt + username + password + IP_address)`

1. Server generates 32 random bytes at startup (secret_salt)
2. Hash computed using OpenSSL SHA256
3. Token stored in in-memory cache: `unordered_map<username, token>`
4. Token validity: Until server restart (stateless deployment)

### Password Hashing

**Algorithm**: glibc `crypt()` with yescrypt prefix (`$y$`)

```cpp
std::string salt = "$y$" + generateRandomSalt(16) + "$";
char* hashed = crypt(password.c_str(), salt.c_str());
```

Yescrypt is memory-hard and CPU-hard, providing security against offline attacks.

### Auth Flow Logic

Handled in `handleAuth()` function (pre-dispatch):

```
handleAuth(endpoint, auth_object, ip_address)
    ↓
if endpoint == "createAccount":
    └─ skip auth, return anonymous

if !auth_object.is_object() OR empty:
    └─ return needs_auth = true

if username empty:
    └─ return error = "username required"

if password supplied:
    ├─ Fetch stored hash from DB
    ├─ If user not found: auth_failed = true
    ├─ Verify password with crypt()
    ├─ If mismatch: auth_failed = true
    ├─ Generate token with SHA256(secret + user + pass + ip)
    ├─ Cache token
    └─ return username + new_token

if token supplied:
    ├─ Lookup username in cache
    ├─ If not found OR mismatch: needs_auth = true
    └─ return username

if neither:
    └─ return needs_auth = true
```

### Response Format

All responses descend from base structure:

```json
{
  "status": "ok",
  "data": { /* endpoint-specific fields */ },
  "needs-auth": false,
  "auth-failed": false,
  "error": null
}
```

**Response interpretation:**
- `data.token`: Included when new token generated (password supplied)
- `auth-failed = true`: Only when password verification fails
- `needs-auth = true`: No auth, expired token, or token mismatch
- `error`: Error message string (present only on error)

## API Endpoints

### createAccount
- **Auth**: Not required
- **Behavior**: Creates user + default character, returns user_id, characters array, and token
- **Response**: `{ "user_id": ..., "username": ..., "characters": [...], "token": "..." }`

### login
- **Auth**: Required (password OR token)
- **Behavior**: Returns user account info and all characters
- **Response**: `{ "user_id": ..., "username": ..., "adult": ..., "characters": [...], token }`

### getCharacter
- **Auth**: Required (password OR token)
- **Behavior**: Returns character data by character_id
- **Response**: `{ "id": ..., "display_name": ..., "safe_display_name": ..., "level": ... }`

### updateUserProfile
- **Auth**: Required (password OR token)
- **Behavior**: Update user account settings (adult flag)
- **Response**: `{ "adult": ..., "token": "..." }`

### updateCharacterProfile
- **Auth**: Required (password OR token)
- **Behavior**: Update character display names (requires character_id, checks adult flag)
- **Response**: `{ "id": ..., "display_name": ..., "safe_display_name": ..., "level": ..., "token": "..." }`

### getGameInfo
- **Auth**: Required (password OR token)
- **Behavior**: Returns game configuration data and image information for all assets. Supports optional filtering via `filters` parameter.
- **Response**: `{ "configs": {...}, "images": {...}, "token": "..." }`
- **Filtering**: Optional `filters` parameter with `asset_types` (config and image types) and/or `asset_ids` (specific asset identifiers)
- **Note**: Returns both `configs` (JSON config data) and `images` (image paths grouped by asset type and action)

### getPlayer, Build, getWorld, getFiefdom, sally, campaign, hunt
- **Auth**: Required (password OR token)
- **Behavior**: Endpoint-specific functionality
- **Token refresh**: Accept password in auth object to generate new token

### getTexts
- **Auth**: Not required (public, pre-auth screens only)
- **Behavior**: Returns translated text for `language` + `text_ids` with NO gender/name substitution (gender tokens resolve to male default)
- **Response**: `{ "texts": { "<id>": "...", ... } }`
- **Note**: The client never sends `sex` to this endpoint. See `getCharacterTexts` for in-game text.

### getCharacterTexts
- **Auth**: Required (password OR token)
- **Behavior**: Returns translated text for `language` + `text_ids`, applying gender substitution (`{male|female}` tokens) and `{character_name}` replacement using the character's stored `sex` and `display_name`
- **Request**: `{ "character_id": ..., "language": "...", "text_ids": [...] }`
- **Response**: `{ "texts": { "<id>": "...", ... } }`
- **Note**: Character must belong to the authenticated user. Client callers use `loadTexts()`/`loadText()` from `client/src/lib/text.ts`, which route here automatically.

## Realtime Combat (WebSocket)

The combat game (PvE 1–32 players; PvP 2–64 when matchmaking lands) is a
server-authoritative RTS over **WebSocket `/ws/combat`** on the same port as
the REST API. Unlike the single-player mini-games, it is not a mini-game: it
has its own Svelte screen and its own transport.

- **RAM-only matches**: matches live entirely in memory (`combat/` module).
  Nothing is written to SQLite during play; after a battle ends, casualties
  are persisted by deferring onto the uWS loop thread (`Loop::defer`) so the
  Database singleton stays single-owner.
- **Threading**: one worker thread per battle from a bounded pool
  (`--combat-sim-threads`, default = CPU count capped at 64). Lobby phases
  consume no worker. The loop thread only enqueues commands into the match's
  mutex-guarded queue and relays chat/voice — it never waits on the sim.
- **Simulation**: 10 ticks/second; entity-level deltas generated by the game
  logic (dirty-set + event outbox), full snapshots every 25 ticks as a sync
  guard. Clients interpolate at rAF speed and can request a full state.
- **Broadcast**: uWS pub/sub topics (`match:<id>`, `match:<id>:team:<n>`,
  `match:<id>:player:<n>`); `App::publish` is thread-safe from the workers.
- **Codec**: wire-neutral `combat_message` structs + pluggable `combat_codec`
  (`json_codec` default with a hand-rolled fast serializer; `binary_codec`
  placeholder). See `docs/combat_protocol.md`.
- **Retinue**: `retinue_members` table (knight = character). Snapshotted into
  the match at join; casualties written back after the battle per the
  ruleset's `death_handling`. See `server/tables/retinue_members.md`.
- **Configs**: `config/combat/rulesets.json` (hot-reloadable via
  GameConfigCache) and `config/combat/maps/` (CombatMapCache, stat()-based
  rescan). Both validated by `tools/check_configs.py`.
- **Voice chat**: WebRTC mesh; the server relays only signaling over the WS
  (`voice` messages). Media never touches the server.

### Combat REST Endpoints

| Endpoint | Purpose |
|---|---|
| `combatCreate` | Create a PvE lobby; returns match id + shareable code |
| `combatJoin` | Join a lobby by code |
| `combatList` | List open lobbies |
| `combatGetConfigs` | Rulesets + maps for the create form |
| `combatMatchmaking` | STUB — PvP queue (challenges/acceptances) is later |
| `getRetinue` | The character's full retinue |

Full protocol and message reference: `docs/combat_protocol.md`.

## Manor (Build & Fiefdom)

The manor is the player's fiefdom board (rendered client-side in
`client/src/components/ManorMenu.svelte`). Building state lives in
`fiefdom_buildings`; the economy tick (`GameLogic::updateStateSince`) runs
production/consumption for the elapsed time whenever fiefdom state is read.

- **`/api/Build`** actions: `build`/`create`, `demolish`, `move`, `upgrade`,
  `upgrade_pond_type` (mill-pond type upgrade earthen → timber → stone), and
  `convert` (stage-chain conversion). Buildings may never overlap river cells
  (build-time rejection). Pond cost/construction arrays resolve from
  `pond_types[pond_type]` via `Validation::getBuildingArrayField`/
  `getNextLevelCost`/`getBuildingMaxLevel`. Upgrade costs are resource-keyed —
  a latent bug once built them with `gold_cost`-style keys, so upgrades never
  actually charged/deducted.
- **Stage chains**: production lines are chains of building types linked by the
  config field `built_from`. `convert` transforms a completed building in place
  to its successor at `max(0, successor_lvl1_cost − 80% × old_cumulative_cost)`
  per resource. Prerequisite/dependency/modifier-target counting is chain-aware
  (a higher stage counts for itself and all lower stages). When the manor house
  (`home_base`) completes an upgrade, the fiefdom's `manor_level` is set to the
  manor house's level, gating `{"manor_level": N}` prerequisites. The manor
  house's `max_level` is **10**, so `manor_level` ranges **0–10** (0 = fresh
  fiefdom, home_base under construction; new fiefdoms default to 0).
- **Arable land** is an abstract, un-rendered resource limiting manor growth
  (no DB column — derived from `manor_level` + the building inventory). Total =
  `economy.json` `arable_land_by_level[manor_level]` (400 at level 1 → 1000 at
  level 10). Each building optionally claims `arable_acres` (15 villein, 30
  freeholder/yeoman, 7 for the 18-grain craft households, 0 for
  infra/industrial/modifier). `/api/Build` (and stage **convert**) rejects with
  `insufficient_arable_land` when `available <` the claimed acres; demolishing
  frees acres. `/api/getFiefdom` returns `arable_land: { total, used, available }`
  and `/api/getBuildingConfigs` injects `arable_acres` per type.
- **Forest land** is an off-map resource analogous to arable, limiting the wood
  producers (no DB column). Total = `economy.json`
  `forest_land_by_level[manor_level]` (200 at level 1 → 600 at level 10). Only
  the woodcutter chain claims `forest_acres`: woodcutter **80**, coppicer **60**,
  timber_hauler **70**. `/api/Build` (and stage **convert**) rejects with
  `insufficient_forest_land` when `available <` the claimed acres; demolishing
  frees acres. `/api/getFiefdom` returns `forest_land: { total, used, available }`
  and `/api/getBuildingConfigs` injects `forest_acres` per type.
- **Classes**: every building type carries a `class` string grouping it
  definitively (independent of the `built_from` chain) — `peasant` covers
  villein/freeholder/yeoman, `flourmill` covers miller/windmill/watermill, etc.
  Modifiers and prerequisites match a `class` target in addition to chain
  matching. `getBuildingConfigs` injects `class`.
- **Build costs** support the production resources too — `charcoal_cost`,
  `iron_cost`, `ironwork_cost`, `fancy_ironwork_cost`, `beams_cost`, and
  `boards_cost` are deducted/refunded
  through the full build/demolish/move/upgrade/convert/refund paths. Build,
  upgrade, and convert **auto-import** material shortfalls: if the fiefdom
  lacks the physical wood/beams/boards/ironwork/etc., the missing amount is
  purchased at the resource's import price with money (respecting the
  per-resource `import_settings` toggle).
- **Money is fungible**: `gold` and `silver_pence` are one wallet at the
  standard rate (1 gold = 240 pence, from the `currency` block). A cost or
  import denominated in pence can be paid with gold (converting gold → pence)
  and vice versa, so a fiefdom with gold but no silver can still buy
  penny-market resources (grain, wood, beams, boards, ironwork). This applies
  to build/upgrade/convert costs and the economy tick's penny-market imports.
- **`/api/getFiefdom`** returns `buildings` (each with `level`, `pond_type`,
  `output_rates`), `river_cells`, `water_power` (building_id → powered),
  `water_power_detail` (`powered_by` + `pond_load`), `road_morale`
  (building_id → points), and the economy report (incl. `net_silver` and
  pence-aware `{amount, pence}` export entries).
- **Water power**: per-fiefdom rivers in `fiefdom_river`, seeded lazily from
  `manor_river.json` templates (rotated 0/90/180/270° deterministically by
  fiefdom id). `Water::computeWaterPower` (`server/WaterNetwork.cpp`): a
  `water_source` mill pond is active when its footprint touches a river cell;
  head races BFS-reach water-powered buildings from the pond; tail races must
  reach the river; a `water_powered` building is powered iff head-reached from
  a pond with spare capacity AND tail-reached from the river. Unpowered water
  buildings produce nothing but still pay `daily_cost`.
- **Economy gating**: production scales by input satisfaction and per-output
  rates (0..1 stored in `output_rates`); road-morale points multiply a
  building's outputs by `1 + points × economy.json.morale_production_multiplier`.
  Production/input `amount`s are level-indexed arrays (+5%/level, +20%/stage)
  and accept numbers, money objects `{gold, shillings, pence}`, or arrays of
  either (money objects normalize to gold); `daily_cost` and the 18-grain
  household outputs stay flat.

## Database Architecture

Two independent SQLite databases for maximum concurrency:

### game.db
- `users`: User accounts (id, username, password_hash, created_at, adult)
- `characters`: Character entities (id, user_id, display_name, safe_display_name, level)
- `fiefdoms`: Character territories (id, owner_id, name, x, y) + resource/state columns
- `fiefdom_buildings`: Building instances (level, x/y, construction, `output_rates`, `pond_type`)
- `fiefdom_river`: Per-fiefdom water-power river cells

### messages.db
- `player_messages`: Direct messages (id, from_character_id, to_character_id, message, timestamp, read)
- `message_queues`: Unread counters (character_id, unread_count)

**Design:**
- No joins across databases
- Concurrent writes to both simultaneously
- Separate file handles for true parallel access

**Initialization:**
- Schema creation and index management handled by `init_db.cpp`
- `initializeGameDB()` creates tables + indexes for game.db
- `initializeMessagesDB()` creates tables + indexes for messages.db
- `initializeAllDatabases()` calls both for complete initialization

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
    gold INTEGER NOT NULL DEFAULT 0,
    silver_pence REAL NOT NULL DEFAULT 0,
    grain INTEGER NOT NULL DEFAULT 0,
    wood INTEGER NOT NULL DEFAULT 0,
    steel INTEGER NOT NULL DEFAULT 0,
    bronze INTEGER NOT NULL DEFAULT 0,
    stone INTEGER NOT NULL DEFAULT 0,       -- retired: stone costs removed from the game (stays 0)
    leather INTEGER NOT NULL DEFAULT 0,
    mana INTEGER NOT NULL DEFAULT 0,
    charcoal INTEGER NOT NULL DEFAULT 0,
    iron INTEGER NOT NULL DEFAULT 0,
    ironwork INTEGER NOT NULL DEFAULT 0,
    fancy_ironwork INTEGER NOT NULL DEFAULT 0,
    beams INTEGER NOT NULL DEFAULT 0,
    boards INTEGER NOT NULL DEFAULT 0,
    wall_count INTEGER NOT NULL DEFAULT 0,
    morale REAL NOT NULL DEFAULT 0,
    last_update_time INTEGER NOT NULL DEFAULT 0,
    manor_level INTEGER NOT NULL DEFAULT 0,
    import_settings TEXT NOT NULL DEFAULT '{}',
    reserves TEXT NOT NULL DEFAULT '{}',
    FOREIGN KEY(owner_id) REFERENCES characters(id)
);

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
    output_rates TEXT NOT NULL DEFAULT '{}',
    pond_type TEXT NOT NULL DEFAULT '',
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);

CREATE TABLE fiefdom_river (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id),
    UNIQUE(fiefdom_id, x, y)
);
```

`manor_level`/`import_settings` (fiefdoms) and `output_rates`/`pond_type`
(fiefdom_buildings) are added by migrations in `init_db.cpp`; `fiefdom_river`
holds the water-power river cells seeded from `manor_river.json`. See
`server/tables/fiefdoms.md`, `server/tables/fiefdom_buildings.md`, and
`server/tables/fiefdom_river.md` for the full reference.

CREATE TABLE officials (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fiefdom_id INTEGER NOT NULL,
    role TEXT NOT NULL,
    portrait_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    level INTEGER NOT NULL DEFAULT 1,
    intelligence INTEGER NOT NULL,
    charisma INTEGER NOT NULL,
    wisdom INTEGER NOT NULL,
    diligence INTEGER NOT NULL,
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

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

The combat game adds `retinue_members` (the player's army — knight = character
plus named soldiers with class/level/gear; see `server/tables/retinue_members.md`).

## Deployment

- **Port**: 2290 (HTTP + WebSocket on the same listener)
- **HTTPS**: Handled by reverse proxy (nginx)
- **Databases**: Created in working directory on first run
- **Nginx config**: See project root README.md

### Nginx WebSocket Proxy

The combat game needs the WebSocket upgrade headers proxied through:

```nginx
location /ws/ {
    proxy_pass http://127.0.0.1:2290;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_read_timeout 3600s;
    proxy_send_timeout 3600s;
}
```

Without the `Upgrade`/`Connection` headers the WS handshake fails; without
the timeouts idle sockets (voice calls, waiting players) get dropped.

### Command Line Options

| Option | Description |
|--------|-------------|
| `--db-dir PATH` | Database directory (default: current directory) |
| `--port PORT` | Port to bind (default: 2290) |
| `--combat-sim-threads N` | Realtime combat simulation worker threads (default: CPU count, capped 64) |
| `--init-db` | Initialize all database tables and indexes, then exit |
| `--create-tables` | Create all database tables, then exit |
| `--ensure-indexes` | Ensure all indexes exist, then exit |
| `--test-num-requests N` | Exit after N requests (agent test mode) |
| `--test-timeout-seconds M` | Exit after M seconds (agent test mode) |
| `--verbose` | Enable verbose logging |
| `--quiet` | Minimal logging |
| `-h, --help` | Show help message |

### Database Initialization

The server uses `init_db.cpp` to manage database schema creation and index management. Three command-line modes are available:

```bash
# Initial database setup (creates tables and indexes)
./server --init-db --db-dir /var/lib/ravenest

# Create tables only (for new databases)
./server --create-tables --db-dir /var/lib/ravenest

# Ensure indexes exist (use after backup/restore)
./server --ensure-indexes --db-dir /var/lib/ravenest

# Normal server startup (auto-initializes)
./server --port 2290 --db-dir /var/lib/ravenest
```

For normal server operation, `initializeAllDatabases()` is called automatically on startup to create tables and ensure indexes.

### Configuration and Image Loading

On startup, the server loads game configuration and image data:

1. **GameConfigCache**: Loads all JSON config files from `config/` directory:
   - `damage_types.json` - Damage type definitions
      - `fiefdom_building_types.json` - Building definitions (production via the `outputs` array — each output with its own `inputs`, `min_level` unlock, and a per-player rate; the `outputs` array is the only production schema)
   - `player_combatants.json` - Player unit definitions
   - `enemy_combatants.json` - Enemy unit definitions
   - `heroes.json` - Hero character definitions
   - `fiefdom_officials.json` - Fiefdom official templates
    - `wall_config.json` - Wall configuration definitions
    - `mini_games.json` - Mini-game definitions including level grids, rewards, replay config, and an optional `image` field (client-facing card image path)
    - `tower_defense/ongoing.json` - Ongoing-mode options for Tower Defense (difficulty/size availability + silver reward tables)
    - `weeding/ongoing.json` - Ongoing-mode options for Assarting (difficulty/size availability + silver reward tables)
     - `economy.json` - Economy config including the `currency` block (old-English ratios: 12 pence/shillling, 20 shillings/pound, 240 pence/gold), `import_prices` (per-resource cost to buy shortfalls — plain numbers are gold prices, money objects `{gold, shillings, pence}` are paid from the silver-pence wallet, e.g. `grain: {"shillings": 1}`; prices deflate as production scales up, anchored to grain), `export_prices` (optional per-resource explicit sell prices, same money-form as import_prices), `export_sell_multipliers` (optional per-resource sell ratios of the import price — e.g. `ironwork: 0.25` sells at 25% of import), `export_sell_multiplier` (0.5 default; excess above reserve sells at `export_prices` → `export_sell_multipliers` → `export_sell_multiplier` × import price), `default_reserves` (per-resource stockpile minimums), `starting_resources` (per-resource starting balances for a newly created fiefdom — currently gold 5, all others 0), `combatant_upkeep_priority`, and `reward_pools` (diminishing-returns limits)
    - `tower_defense/maps/` - Tower defense map metadata JSON files (dynamic: directory is rescanned on each request, allowing hot-reload of new maps without server restart)
    - `combat/rulesets.json` - Realtime combat mission rules (mode, death handling, caps, duration — see `docs/combat_rulesets.md`)
    - `combat/maps/` - Realtime combat map files (CombatMapCache, stat()-based hot reload — see `docs/combat_maps.md`)
    - `manor_river.json` - Per-fiefdom river templates (meandering polyline `points` + band `width`; seeded with 0/90/180/270° rotation per fiefdom — see `server/tables/fiefdom_river.md`)
    - `manor_ui.json` - Manor UI config (`build_order` governs the build-palette button order only)

 2. **TowerDefenseMapCache**: Dynamically loads tower defense map metadata from `config/tower_defense/maps/`. Each `.json` file follows the map metadata format documented in `/tower_defense_map_metadata_format.md`. The directory is rescanned when its modification time changes, so new maps can be added at runtime without restarting the server. Maps are served to clients as `map_metadata` in `/api/startMiniGame` responses.

 3. **ImageCache**: Scans the `images/` directory to build an in-memory cache of all available images:
   - `images/buildings/{building_id}/{action}/{frame}.png`
   - `images/combatants/{combatant_id}/{action}/{frame}.png`
   - `images/heroes/{hero_id}/{action}/{frame}.png`
   - `images/heroes/{hero_id}/skills/{skill_id}/{frame}.png`
    - `images/portraits/{portrait_id}/{frame}.png`
    - `images/tower_defense/maps/{map_filename}.png` - Tower defense map background images (served via dedicated GET route at `/images/tower_defense/maps/*`)

 The `TowerDefenseMapCache` is separate from the `ImageCache` because maps are loaded dynamically and served as JSON metadata (not enumerated image assets). Map background images are served directly via a static file GET route, bypassing the ImageCache entirely.

 All caches are used by their respective endpoints to provide complete game data to clients.

## Mini-game Rewards & Diminishing Returns

Ongoing-mode games (level 0, played outside the marches) pay **silver** rewards
denominated in pence and formatted in old-English (12 pence/shillling, 20
shillings/pound). The server is authoritative for rewards:

- Options (available difficulties/sizes) and base reward tables come from
  `tower_defense/ongoing.json` and `weeding/ongoing.json`; the server rejects
  any `difficulty`/`rounds`/`grid_size` not offered by these configs.
- Reward formula: `base_pence = size_reward_pence + difficulty_coeff_pence × (difficulty − 1)`.
- The client queries `/api/estimateOngoingRewards` (read-only) to preview the
  expected payout, which is adjusted for the character's reward pool.
- **Diminishing returns** are enforced entirely server-side and shared across
  all ongoing mini-games. A per-character `reward_pools` row (lazily
  replenished from `last_consumed_at`, 5/day, never written by estimate calls)
  gives the first 15 wins full rewards, the next 5 half (rounded down, min 1
  penny), and everything after a quarter (rounded down, min 1 penny).
- Payouts are computed at completion from server-owned session data (TD:
  `game_sessions.difficulty`/`total_rounds`; weeding: session `difficulty`/`grid_size`)
  and credited to the fiefdom's `silver_pence` balance when the character has a
  fiefdom (during `land_patent`, pre-manor, rewards are display-only).

## Future Enhancements

| Feature | Status | Notes |
|---------|--------|-------|
| Salted password hashes | Implemented | Yescrypt ($y$) with random salt |
| Session timeout | TODO | Tokens valid until restart per design |
| Rate limiting | TODO | Not yet implemented |
| API versioning | TODO | Current endpoints unversioned |