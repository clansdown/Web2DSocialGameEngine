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

The combat game (PvE 1–8 players; PvP 2–64 when matchmaking lands) is a
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
- **Per-class body size**: every combatant config carries a required `size`
  (normalized board units; 0.03 = one standard soldier). It is the sim's physical
  radius (hard-body collision, deploy spacing, structure adjacency →
  `combat_unit_stats.radius`) **and** the client's drawn sprite/token size,
  independent of the source image dimensions. `combatGetConfigs.combatant_radius`
  catalogs it per class; each in-battle unit row carries it as `radius`.
- **Broadcast**: uWS pub/sub topics (`match:<id>`, `match:<id>:team:<n>`,
  `match:<id>:player:<n>`). uWS is single-threaded — the **only** thread-safe
  entry point is `Loop::defer` — so worker threads never call `App::publish`
  directly; they post the publish onto the loop, which performs it on the loop
  thread. The App's address is held in a shared atomic slot that is nulled
  before the App is destroyed, so a late post is a no-op rather than a
  use-after-free.
- **Shutdown**: the server uses uWS's **fall-through** model. A signal
  (SIGINT/SIGTERM) or `--shutdown-after-ms N` (tests) sets a flag; a monitor
  thread posts a close-all (`Loop::defer`) that closes every combat socket and
  the listening socket. `App::run()` then returns; the server stops the combat
  worker pool and joins it *before* the App/loop is destroyed. Worker threads are
  joinable; `combat_match_manager::shutdown()` also flags live matches to stop so
  the join is prompt (a stopped match persists no result).
- **Codec**: wire-neutral `combat_message` structs + pluggable `combat_codec`
  (`json_codec` default with a hand-rolled fast serializer; `binary_codec`
  placeholder). See `docs/combat_protocol.md`.
- **Retinue**: `retinue_members` table (knight = character). Snapshotted into
  the match at join; casualties written back after the battle per the
  ruleset's `death_handling`. See `server/tables/retinue_members.md`.
- **Retinue portraits**: each member carries a fixed `headshot_id` (path under
  `game/images/retinue/headshots/`), assigned at hire by the deterministic
  market (names/headshots re-roll past identities already in the roster).
  Pools are scanned + sorted at startup by `server/AvatarPool.*` (no catalog) and
  served at `/images/retinue/*`; frame overlays live at
  `game/images/retinue/frames/{recruit,management}.png`, with their geometry
  (overlay image + pixel size + portrait destination rect) cataloged in
  `game/config/portrait_frames.json` and served by `/api/getPortraitFrames`
  (frame drawn on top — see `server/docs/portrait_frames.md`). `getRetinue` returns a
  server-resolved `portrait_url` per member (soldiers → headshot; knight → the
  owner's PFP). The RTS does **not** use these portraits.
- **Player PFP (variety only)**: a layered identity, mirroring the safe/custom
  name model. `characters.avatar_safe_id` is the always-present safe pick (shown
  to minors and used as the fallback for every viewer); `avatar_custom_id` is an
  optional 18+ upload layered on top for adult viewers; `avatar_storage` selects
  `local` (under `uploads/avatars/`) or `r2` (the public assets bucket);
  `avatar_changed_at` drives a 24h per-character cooldown (the creation pick is
  exempt). URL resolution is shared by every surface (`server/AvatarUrl.cpp`,
  `resolve_avatar_url`): custom only when **both** owner and viewer are adults,
  else the safe pick. Safe art is public at `/images/avatars/safe/{male,female}/*`
  (gender-split like the headshots; the client requests the character's own pool;
  scanned at startup and auto-rescanned when the pool directory changes — no
  restart needed). Custom keys are content-addressed
  (`<character_id>/<sha256>.<ext>`); the local backend is served only by the
  age-gated `GET /api/avatar/<character_id>`, while R2 returns an absolute
  capability URL under the assets origin. Uploads are validated and written on a
  bounded worker pool; R2 PUT/DELETE and Cloudflare PURGE use the minimal
  SigV4 client in `server/avatars/`. Endpoints: `getAvatarPool`,
  `setCharacterAvatar`, `uploadCharacterAvatar`, `removeCustomAvatar`. The hub
  **Profile** activity (`client/src/components/ProfilePanel.svelte`) is the
  change/upload UI; the creation step picks a safe portrait (or skips when the
  pool is empty). **Moderation of custom uploads is a TODO.** Public capability
  URLs require the separate F7 age-isolation gate before production.
- **Configs**: `config/combat/rulesets.json` (hot-reloadable via
  GameConfigCache) and `config/combat/maps/` (CombatMapCache, stat()-based
  rescan). Both validated by `tools/check_configs.py`.
- **Voice chat**: WebRTC mesh; the server relays only signaling over the WS
  (`voice` messages). Media never touches the server.
- **Art (optional)**: combat renders procedurally by default (class-colored
  circles + the mandatory team ring / HP bar / facing-chevron overlays). Real
  sprites drop in behind the same API (`client/src/combat/unit_sprites.ts`):
  - Legacy flat, rotatable `game/images/combatants/<class>/idle/1.png`.
  - Directional clips `game/images/combatants/<class>/<clip>/<set>/<n>.png`
    (StarCraft-style, up to 8 views). The `animations` block in the combatant
    config gives `fps` and per-direction `views` (a set name, or `{set, mirror}`
    — mirroring is opt-in, so authoring all 8 sets needs no `views`). The frame
    count is discovered from the directory contents; `idle` (usually a single
    image) is the standing pose drawn while stationary. `combatGetConfigs`
    advertises only clips/sets that exist, so the client never requests missing
    art; classes without art keep the procedural draw.
  Recruit/roster class icons use the same flat `idle/1.png` URL and hide
  themselves when absent.
  Sprites are authored facing **right (+x)**, transparent, with no baked team
  color, ring, HP bar, or shadow (those are overlays).

#### uWS rules (sharp edges — follow these)

uWS is extremely fast precisely because it makes lifetime/threading assumptions.
When touching `main.cpp`:

1. **Never call into uWS from a non-loop thread** except `Loop::defer`. Publish,
   send, subscribe, and `App` access all happen on the loop thread.
2. **Never store `req`/`res` views past the synchronous handler.** `req->getUrl()`,
   `req->getHeader()`, etc. return `std::string_view` into the connection's
   receive buffer, which is reused. Copy into owned `std::string`s first — see
   `ClientInfo` (`ApiHandlers.hpp`), which is captured into `res->onData`.
   `res->end(...)` is safe with a local string: it copies into the socket buffer
   before returning.
3. **Keep producers inside the App's lifetime.** Worker threads must be stopped
   and joined before the App/loop is destroyed (`combat_match_manager::shutdown`).
4. **Use fall-through to stop the loop** — close all sockets + the listener. Do
   not invent a stop API.
5. **Avatar uploads** run base64/decode/hash/dimension checks and the R2 PUT on
   `avatar_upload_manager`'s bounded pool. The completion *always* returns via
   `Loop::defer` before reading/writing SQLite or touching `res`. Join the pools
   during shutdown, before the App/loop is destroyed.

Concurrency regressions are covered by `tools/combat_shutdown_test.sh` (graceful
shutdown with a live battle) and `tools/combat_broadcast_stress.sh` (repeated
match churn). A ThreadSanitizer build is available for the same tests:
`./compile_debug-server.sh --tsan` → `server/build-tsan/server` (or `--asan`
for Address+UBSan; see `./compile_debug-server.sh --help`).

### Combat REST Endpoints

| Endpoint | Purpose |
|---|---|
| `combatCreate` | Create a PvE lobby; returns match id + shareable code |
| `combatJoin` | Join a lobby by code |
| `combatList` | List open lobbies |
| `combatGetConfigs` | Rulesets + maps + mission opportunities (warband fixed at create) + structures/formations catalogs + authored combatant art (directional clips) |
| `combatMatchmaking` | STUB — PvP queue (challenges/acceptances) is later |
| `getRetinue` | The character's full retinue + manor capacity |
| `listRecruitCandidates` | The current hire market (named candidate offers) |
| `hireRecruit` | Hire a candidate (verified offer, capacity, fee) |
| `setRetinuePriority` | Reorder the roster by strict funding/healing priority |

Full protocol and message reference: `docs/combat_protocol.md`.

**Determinism:** a per-match seeded PRNG drives all dodge/AI randomness. Pass
`--combat-seed N` to make every match reproducible; `match_ended` then carries
the `seed` and a stable `state_hash`. Pass `--combat-replay-dir DIR` to record
each finished match's tick-exact command/ready log to `DIR/<match_id>.json`.
`--combat-replay FILE` re-runs a recorded match synchronously and prints its
`state_hash`; `tools/replay_combat.sh` uses both to assert a recorded match
replays to an identical hash.

## Premium Store (Platinum & Cosmetics)

The premium-variety surface (design: `docs/rts_design.md` §15). **Platinum is a
separate ledger, completely non-fungible with `gold`/`silver_pence`** and never
usable in the normal economy — real money buys variety, never power. Cosmetics
are **character-bound** and appearance-only; a deny-by-default whitelist in the
config linter makes it impossible for a cosmetic to carry a gameplay key.

| Endpoint | Purpose |
|---|---|
| `getStore` | Catalog (items/bundles/products) + Platinum balance + owned set |
| `getCosmetics` | Owned cosmetics + equipped loadout + balance |
| `purchasePremiumItem` | Buy a cosmetic/bundle with Platinum (idempotent, server-authoritative) |
| `purchasePlatinum` | Start a real-money purchase (gateway **stub** → pending intent + transaction) |
| `settlePlatinum` | Mock gateway webhook — settle a pending purchase, granting Platinum exactly once |
| `equipCosmetic` / `deassignCosmetic` | Validate slot + ownership, then set/clear a loadout slot |
| `refundPremiumItem` | Revoke ownership + clear the loadout slot (support action; credits no Platinum) |

Storage: `platinum_ledger`, `platinum_transactions`, `character_cosmetics`,
`character_loadout` (see `server/tables/`). Catalog: `game/config/cosmetics.json`.
Adventure *access* purchase and the monthly-pass fulfilment are **deferred**
(`rts_design.md` §18).

## Progression & Tenure

After the land patent a character chooses a tenure rather than being locked into
one. State lives in `player_game_state` (`freehold` / `freehold_qualified`
phases), `baronies.defunct_at`, `character_letters`, and
`character_milestones`; gates are in `game/config/progression.json`.

- **Outlying Freehold** (`/api/foundFreehold`): a lone manor with no barony
  membership (`freehold`, `base_unlocked = 1`). A character owns at most one
  fiefdom, created lazily and **persistent** across tenure changes.
- **Qualification & the king's letter**: once a freeholder's manor house reaches
  the configured level and they hold the configured RTS milestone
  (`progression.json` `mesne_lord.requirements` — default manor 6 +
  `ettin_slain`), the next `/api/getPlayerState` lazily promotes them to
  `freehold_qualified` and sends a one-time letter
  (`letter_mesne_lord_eligible`). Letters are read via `/api/getLetters` /
  `/api/markLetterRead`; the hub shows a `letters` activity and unread overlay.
- **Honor / Mesne Lord** (`/api/applyForMesneLord`): a qualified freeholder
  founds an Honor (a `baronies` row + a `mesne_lord` membership on the existing
  manor) and enters the shared `sandbox` state.
- **Fluid tenure**: `/api/leaveBarony` lets any member leave. A founder with
  other members must name a successor (ownership + leadership reassigned); a
  solo founder tombstones the barony (`baronies.defunct_at` — name reserved,
  hidden, chat archived). Leaving stamps a join cooldown
  (`player_game_state.barony_left_at`, `progression.json`
  `barony.join_cooldown_days`, default 3 days) enforced by `/api/joinBarony` and
  reported as `barony_join_cooldown_until`. `/api/joinBarony` accepts
  `land_patent`, `freehold`, and `freehold_qualified`; `/api/getBaronies` hides
  defunct baronies; `/api/getBaronyMembers` lists members in join order.
- **Boss milestones**: `progression.json` `milestones` are counter-based
  conditions (`{ "id": "ettin_slain", "counter": "slain.ettin", "min": 1 }`).
  Enemy deaths are tallied per class into `character_counters` after every
  combat attempt (`combat_match_manager::on_match_end`), and any satisfied
  condition is granted to `character_milestones`. Because generated high-tier
  offerings can field the boss-tier Ettin, `ettin_slain` is reachable and is
  granted purely on the Ettin being **slain** (regardless of the overall
  outcome).

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

## Chat

Server-authoritative chat (DM, group, barony, dukedom, wildlands) with
per-character membership, aggregate delta polling, integrated translation, Web
Push, and admin identity locks. Full reference: `server/docs/chat.md`.

- **Transport**: polling-first REST (`pollChats` / `pollWildlands`); the delta
  envelope is transport-agnostic so a future `/ws/chat` is additive.
- **Storage**: a dedicated `chat.db` (see Database Architecture below).
- **Translation**: baseline MADLAD-400-3B via an internal localhost sidecar
  (`server/translation/`), with a stubbed premium LLM engine behind the same
  seam. Degrades to the original text when the sidecar is down.
- **Push**: VAPID + RFC 8291 `aes128gcm` (`server/chat/PushSender.cpp`); the
  service worker lives at `client/public/sw.js`.
- **Config**: `game/config/chat.json`, `chat_admins.json`, `translation.json`,
  `push.json`, `safe_messages.json`, `safe_reactions.json`, `safe_gifs.json`.
- **Minor visibility**: minors see/author only server-owned safe content. Custom
  messages are omitted entirely and never affect their unread/preview/push; see
  `docs/minor_content_visibility.md`. Group chats are adult-only.

Chat endpoints: `getChats`, `getChatMessages`, `pollChats`, `pollWildlands`,
`sendChatMessage`, `editChatMessage`, `deleteChatMessage`, `markChatRead`,
`setChatSettings`, `openDirectChat`, `searchChatCharacters`,
`requestTranslations`, `getChatLanguagePrefs`, `setChatLanguagePref`,
`addChatReaction`, `removeChatReaction`, `addChatBookmark`, `removeChatBookmark`,
`getChatBookmarks`, `sendFriendRequest`, `acceptFriendRequest`,
`declineFriendRequest`, `unfriend`, `blockUser`, `unblockUser`, `muteCharacter`,
`unmuteCharacter`, `getFriends`, `createGroupChat`, `inviteToGroup`,
`acceptGroupInvite`, `declineGroupInvite`, `leaveChat`,
`transferGroupOwnership`, `setGroupModerator`, `kickMember`, `renameGroup`,
`setGroupInvitePolicy`, `banFromChat`, `unbanFromChat`, `getModerationLog`,
`chatHeartbeat`, `getFriendPresence`, `setChatTyping`, `getPushConfig`,
`subscribePush`, `unsubscribePush`, `setPushPreferences`,
`adminGetIdentityHistory`, `adminLockIdentity`. Each has a doc in `api/`.

## Database Architecture

Three independent SQLite databases for maximum concurrency:

### game.db
- `users`: User accounts (id, username, password_hash, created_at, adult)
- `characters`: Character entities (id, user_id, display_name, safe_display_name, level)
- `fiefdoms`: Character territories (id, owner_id, x, y) + resource/state columns.
  Fiefdoms are **unnamed**; the legacy `name` column is internal-only and never
  exposed.
- `fiefdom_buildings`: Building instances (level, x/y, construction, `output_rates`, `pond_type`)
- `fiefdom_river`: Per-fiefdom water-power river cells
- `player_game_state`: Progression phase + `honor_name` + `barony_left_at` (tenure cooldown)
- `character_letters`: In-game letters (text-system key + read state)
- `character_milestones`: Per-character progression milestones (e.g. `ettin_slain`)
- `baronies` / `barony_members`: Barony tenure (incl. `baronies.defunct_at` tombstoning)

### chat.db
- The entire chat system: `chats`, `chat_members`, `chat_invites`, `messages`,
  `message_versions`, `message_translations`, `message_reactions`, `bookmarks`,
  the social graph (`friends`, `friend_requests`, `blocks`, `character_mutes`,
  `character_relation_counts`), moderation (`chat_bans`, `moderation_log`),
  rate limiting (`rate_limit_violations`, `rate_limit_timeouts`), push
  (`push_subscriptions`, `push_preferences`), and `chat_language_prefs`.
- Full reference: `server/docs/chat.md` and `server/tables/*.md`.

**Design:**
- No joins across databases
- Concurrent writes to all simultaneously
- Separate file handles for true parallel access
- Chat uses a global `change_id` cursor for delta polling

**Initialization:**
- Schema creation and index management handled by `init_db.cpp`
- `initializeGameDB()` creates tables + indexes for game.db
- `initializeChatDB()` creates tables + indexes for chat.db
- `initializeAllDatabases()` calls both for complete initialization

### game.db Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    adult INTEGER NOT NULL DEFAULT 0,
    language TEXT NOT NULL DEFAULT 'en'
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

### chat.db Schema

Chat has its own database with ~21 tables. Rather than duplicate the full DDL
here, see the architecture doc and per-table references:

- `server/docs/chat.md` — storage model, polling, translation, push, moderation
- `server/tables/change_counter.md`, `chats.md`, `chat_members.md`,
  `chat_invites.md`, `messages.md`, `message_versions.md`,
  `message_translations.md`, `message_reactions.md`, `bookmarks.md`,
  `friends.md`, `friend_requests.md`, `blocks.md`, `character_mutes.md`,
  `character_relation_counts.md`, `chat_bans.md`, `moderation_log.md`,
  `rate_limit_violations.md`, `rate_limit_timeouts.md`,
  `push_subscriptions.md`, `push_preferences.md`, `chat_language_prefs.md`
- `character_identity_history` lives in **game.db** (admin identity lock):
  `server/tables/character_identity_history.md`

The combat game adds `retinue_members` (the player's army — knight = character
plus named soldiers with class/level/gear; see `server/tables/retinue_members.md`).

## Deployment

- **Port**: 2290 (HTTP + WebSocket on the same listener)
- **HTTPS**: Cloudflare Tunnel in production (or nginx as an alternative)
- **Databases**: `game.db`, `chat.db`, and `staff.db` are created in `--db-dir` (default: the working directory) on first run. `staff.db` holds staff roles/accounts, the shared action journal, support tickets, notifications, and banned avatar hashes.
- **Assets**: `config/`, `images/`, and `text/` are read from the `--content` root
  (default: the working directory). Pointing `--content` at `game/` while running
  from a scratch directory keeps databases and uploads out of the synced assets
  (see `run_server.sh`).
- **Publishing / tunnel setup**: See `docs/publishing.md`

### CORS

The browser client is served from a separate origin (Cloudflare Pages) than
the API. The API answers CORS preflights and includes the allowlist header on
API responses. Configure the allowlist in the server service environment:

```bash
RAVENEST_CORS_ORIGINS="https://app.example.com"
```

- Comma-separated origins (scheme + host, no trailing slash).
- Empty/unset means `*`; this is safe for the current API because it uses
  explicit JSON/query credentials and no cookies.
- `OPTIONS /api/*` returns `204 No Content`.
- `GET /api/avatar/<id>` is sent with `Cache-Control: private, no-store`.

`install.sh` writes this value to the server env file from `.deploy` and
optionally restarts the service. See `docs/publishing.md`.

### Avatar storage

`RAVENEST_AVATAR_STORAGE=local` (default) writes to `uploads/avatars/` in the
server's working directory. `r2` requires the complete R2 configuration:

```bash
RAVENEST_AVATAR_STORAGE=r2
RAVENEST_R2_ENDPOINT=https://<account>.r2.cloudflarestorage.com
RAVENEST_R2_BUCKET=ravenest-assets
RAVENEST_R2_ACCESS_KEY_ID=...
RAVENEST_R2_SECRET_ACCESS_KEY=...
RAVENEST_AVATAR_R2_PREFIX=avatars/
RAVENEST_AVATAR_PUBLIC_BASE_URL=https://assets.example.com
RAVENEST_AVATAR_UPLOAD_THREADS=2
RAVENEST_AVATAR_MAINTENANCE_THREADS=1
RAVENEST_AVATAR_QUEUE_MAX=64
```

Optional purge credentials (`RAVENEST_CLOUDFLARE_ZONE_ID`,
`RAVENEST_CLOUDFLARE_API_TOKEN`) delete the edge copy after removal. The server
fails startup if `r2` is selected with incomplete configuration. Use a separate
scoped token from the deploy token. See `docs/publishing.md` for the Cloudflare
cache rule and production release gate.

### Cloudflare Tunnel / Nginx

Production usually enters through a Cloudflare Tunnel:

```yaml
ingress:
  - hostname: api.example.com
    service: http://127.0.0.1:2290
  - service: http_status:404
```

The tunnel forwards both `/api/*` and `/ws/*` on the API host, including the
WebSocket upgrade. As an alternative, terminate TLS with nginx and proxy:

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
| `--content PATH` | Asset root holding `config/`, `images/`, `text/` (default: current directory) |
| `--db-dir PATH` | Database directory (default: current directory) |
| `--text-dir PATH` | Text directory (default: `<content>/text`) |
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

On startup, the server loads game configuration and image data from the
`--content` root (default: the working directory); `config/`, `images/`, and
`text/` are resolved under it:

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
    - `combat/warbands.json` - Mission opportunities (ruleset + map + fixed warband + tier + XP base — enabled content)
    - `combat/structures.json` - Temporary combat structures (barricade/spikes/wall/tower + garrison buffs)
    - `combat/formations.json` - Formation shapes (line/column/wedge/circle + spacing/rotation)
    - `combat/profiles.json` - Enemy AI per-class fight styles + group behavioral axes (tactic/aggro/retreat/siege)
    - `combat/rewards.json` - Rewards pipeline (XP curve, victory multiplier, morale, empty gold/equipment/cosmetics slots)
    - `cosmetics.json` - Premium-variety catalog: six slots, tiers, `price_platinum`, `source`, appearance descriptors; whitelist-validated (no gameplay keys)
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