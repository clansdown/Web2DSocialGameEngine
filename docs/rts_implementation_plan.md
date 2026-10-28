# Ravenest RTS — Implementation Plan

**Status:** Finalized. This is the staged implementation plan for the realtime
combat game. **Source of truth:** `docs/rts_design.md` — this plan turns that
design into a staged, always-compiling build. Each milestone ships a testable
slice; nothing lands "half-fed."

**Progress:**
- ✅ **M0 — Baseline** — complete; server compiles, linter clean.
- ✅ **M1 — Simulation core** — complete and **WS-smoke verified** (`tools/combat_smoke.sh`). Found & fixed a real bug: melee `attack_range` must ≥ 2× hard-body radius (0.07 ≥ 2×0.03).
- ✅ **M2 — Mission model & deploy** — complete and smoke-verified incl. `persisted xp` check.
- ✅ **M3 — Temporary structures** — channeled construct (solid immediately, HP ramp, ~2 builders, interrupt aborts), non-`blocks_ranged` ranged blocking, garrison (arrow-slit, melee-immunity, +dmg/+range buff, expel/empty on destroy), melee structure demolition. Smoke-verified (construct → `structure_spawned` → `structure_built` → battle → XP). Fixed a real bug: fresh builds spawn at 0 HP; the destroy-on-≤0 check must be gated on having taken damage.
- ✅ **M4 — Enemy AI** — profile-driven group AI + warband battle state: contact activates groups per `aggro_model` (local/call_to_arms/global), goblin swarm surround (back-arc fans), bandit/dark-mage skirmish range-keeping, orc retreat threshold, guard/charge tactics, siege fallback targeting blocking structures. Smoke-verified (goblins now pin the sole knight — emergent, correct).
- ✅ **M5 — AoE & the alchemist** — `alchemist` player combatant (+ enemy `flask_lobber`), AoE damage class (uniform splash, no arcs/dodge, magical flat mitigation, wall shelter), safe-center over-throw (cap = splash radius, own body friendly, fire-through cleared / walls-towers protected), hold-fire + status event, explicit **Lob** ground point. Smoke-verified (`aoe_events=3`).
- ✅ **M6 — Formations** — persistent per-player `formation` modifier; Move/Attack-Move slot into line/column/wedge/circle offsets with a rotation handle. Regression-clean.
- ✅ **M7 — Client battle experience** — `CombatGame.svelte` renders structures (HP/build bars), procedural class sprites with **mandatory team ring + HP bar + facing chevron** overlays, a **pending-damage ledger** (arrow-synced damage numbers, rear-arc tinted), AoE blast rings, a hold-fire icon, a **risky-Lob friendly-fire preview** (blast circle + caught friendlies glow), blocked-arrow-to-wall tracers, and selection feedback. Selection: click / drag-box / shift-add / double-click type-select / Ctrl+1..9 control groups (control groups own Ctrl+1..9; formations use the palette). Camera: drag-pan, wheel + pinch zoom (clamped), WASD/arrows, **toggleable edge scroll**, minimap click-jump. `CombatCommandBar.svelte` (selection info incl. front/side/back defense & dodge; Move/**Attack-Move**/**Patrol**/Attack/Lob/Stop/Hold/Stance/Construct/Formation + queue pill). Touch: **smart-tap vs context-panel** modes with long-press-drag pan + on-screen box select.
- ✅ **M8 — Client mission flow** — `MissionPick.svelte` (opportunity browse with warband preview/flavor, open-lobby browser, join-by-code), `RosterSelect.svelte`, in-lobby `MatchLobby.svelte` (roster + ready), `DeployView.svelte` (drag placement inside the team `deploy_zone`, facing, Ready/auto-deploy, per-unit non-overlapping server placement), `ResultsView.svelte` (banner/reason, per-member XP + level-ups, wounds). Server applies the host's create-time roster subset and sends the **fielded** roster + heraldic `banner` in the welcome.
- ✅ **M9 — Entry & integration** — `combat` hub activity card gated behind `land_patent` (`activity_combat` text), `#/activity/combat` → CombatScreen.
- ✅ **M10 — QA & docs close** — compile/linter/client-check + both smoke regressions green; API + AGENTS.md docs updated. **Determinism:** `--combat-seed` + a stable `state_hash` in `match_ended`; `--combat-replay-dir` records a **tick-exact command/ready log** per match and `--combat-replay FILE` re-runs it synchronously — `tools/replay_combat.sh` asserts the recorded match replays to an identical hash. Balance values documented.
- ✅ **M11 — Platinum & cosmetics server** — separate non-fungible Platinum ledger (`platinum_ledger`/`platinum_transactions`), character-bound ownership + loadout (`character_cosmetics`/`character_loadout`), `cosmetics.json` catalog with a **deny-by-default whitelist** linter (no gameplay keys — the no-pay-to-win proof), store REST (`getStore`/`getCosmetics`/`purchasePremiumItem`/`purchasePlatinum`/`settlePlatinum`/`equipCosmetic`/`deassignCosmetic`), `rewards.json` cosmetics seam. Verified by `tools/monetization_smoke.sh` (idempotent purchase + settle grants exactly once) and a seeded gameplay-key lint rejection.
- ✅ **M12 — Client cosmetics** — `StorePanel.svelte` (browse by slot, buy with Platinum, equip/deassign, mock Platinum top-up), store/cosmetics API wrappers, and procedural appearance rendering (`palette`/`pattern`/`emblem`) on units + structures with the mandatory overlays intact (no camouflage) and flourish tints on attack/AoE/structures. Server welcome carries `banner` + per-member `cosmetic` (appearance only; never tick deltas).
- ✅ **M13 — Monetization QA & close** — `refundPremiumItem` (revokes ownership + clears the loadout; idempotent), extended `tools/monetization_smoke.sh` (refund + no-pay-to-win whitelist proof), docs close. No CI artifact — per-milestone manual gates are documented in §17.

**Scope:** v1 scope per `rts_design.md` §2 — squad tactics, annihilation PvE,
directional defense, temporary structures, AoE/alchemist, enemy AI, formations,
deploy phase, XP/morale. Out of scope (deferred, design §18): player abilities,
PvP/scrimmage, fog, `penetrates_walls` spells, TURN voice relay, binary codec.

**Confirmed planning decisions:**
- **Granularity:** 13 milestones, each with explicit exit criteria and a
  compiling/testable state.
- **Entry point (D1):** a new `combat` hub activity card (`activity_combat`
  text id) gated behind the `land_patent` game phase (a manor + retinue exist),
  wired in Milestone 9 as `#/activity/combat/...`.
- **Balance (D2/D6/D7):** shape-now, tune-later. Configs carry correct shapes
  and valid defaults from Milestone 0; the real numbers pass lands in
  Milestone 10. v1 opportunity set = 4 (goblin/orc/bandit/troll, tiers 1–4).
- **Authoring-time defaults (no sign-off needed):** roster timing = host picks
  at mission pick, joiners pick in the lobby before ready (D4); `attack_type`
  derives from the highest damage stat with explicit authoring override (D5);
  `direction_defense`/`dodge` are class-level in v1 with per-member override
  deferred (D8).

---

## 0. Design ↔ Code Consistency Baseline

These are the gaps between `rts_design.md` and today's scaffold. Milestone
ownership is explicit; this table is the checklist the milestones map back to.

| # | Gap | Design anchor | Milestone |
|---|---|---|---|
| C1 | Skirmish ruleset has `win_conditions: ["survive_until_end"]`; must be annihilation (`warband_wiped`); `match_duration_seconds` becomes the **safety cap** (draw at cap unless warband wiped) | §3.2, §3.4 | M0 |
| C2 | Team currently derived via `player_team(owner)` → returns `-1` for non-players → enemy units (owner 0, team 2) would break `active_teams`/`alive_count_for_team` | §5.1, §13.3 | M1 |
| C3 | No enemy identity — all spawned units are retinue members with positive ids | §13.3, §16.3 | M1 |
| C4 | Phases are `lobby → countdown → battle → ended`; design inserts **deploy** (placement + facing + ready) | §4.4, §13.1 | M2 |
| C5 | Maps have no `deploy_zone`; spawn is two scatter points | §4.4, §17.1/17.2 | M2 |
| C6 | HP bridge absent: `max_hp = 100` hardcoded; retinue snapshot lacks health% | §5.2 | M1 |
| C7 | Movement hardcoded `unit_speed_ = 0.025`; design base speed 0.04 × `movement_speed` | §5.3 | M1 |
| C8 | Combatant config lacks `max_hp`/`attack_range`/`attack_interval`/`attack_type`/`dodge`/`direction_defense`/`turn_rate` | §5.1, §5.4 | M0 |
| C9 | "Attack" is relayed as an event; no damage, cooldowns, arcs, dodge, LOS, melee rings | §5.4–5.7, §10 | M1 |
| C10 | No enemy warband concept; no spawn of enemies | §3.1, §11 | M1 (spawn) / M2 (opportunities) / M4 (AI) |
| C11 | No structures (no `structures` wire array, no build, no blocking, no garrison) | §7 | M3 |
| C12 | No AoE damage class, alchemist, over-throw/hold-fire/Lob | §6 | M5 |
| C13 | No formations | §8.3 | M6 |
| C14 | No XP on retinue members (`retinue_members` has no `xp` column); `match_ended` lacks XP/level-up/rewards payload | §14 | M2 |
| C15 | Draw semantics: `on_match_end` stamps `last_victory_ts`/`last_defeat_ts` for everyone; must be 1/0/-1 winners, draw stamps neither, forfeit = defeat | §3.4/3.5, §14.3 | M2 |
| C16 | Sim thread reads no config; match ctor gets only ruleset + map metadata. GameConfigCache is a hot-reload singleton — the sim needs a **snapshot** | §16.4 | M1 |
| C17 | Client: no combat entry in the app flow ("no hub cards"); design journey begins at "Combat entry → Mission pick" | §4, §13 | M8/M9 |
| C18 | Wire: unit rows lack `facing`; no `structures` array; `attack` events lack `result` | §17.1 | M1–M3 incremental |
| C19 | Client selection is click-only; no box/double-click/type-select, control groups, camera kit, minimap, command bar | §12.2–12.4 | M7 |
| C20 | Lobby create takes free `ruleset_id`+`map_id`; design: **opportunity** (ruleset+map+warband+tier) fixed at create | §3.1, §4.1 | M2/M8 |

### Threading rule (regression-proofing)

The match worker thread must **never** touch `GameConfigCache`, `FiefdomFetcher`,
or SQLite directly. All config the sim needs is resolved on the uWS loop thread
at create/join time into a `combat_match_config` snapshot (JSON copies) passed
into the match constructor (C16). Persistence continues via `Loop::defer`
(design §16.4). This is the single most important invariant for any combat work:
violating it reintroduces cross-thread data races on a hot-reload singleton.

---

## 1. Milestone Overview

```
M0  Baseline: docs alignment + combatant config schema + new config skeletons + linter
M1  Simulation core: stats/HP bridge, movement+collision, combat resolution, arcs/dodge,
    end conditions, winner semantics, enemy spawn (idle), seeded PRNG
M2  Mission model: opportunities, warband config, roster select, deploy phase + deploy_zone,
    XP/morale/rewards persistence, conclusion payload
M3  Temporary structures: build channel, blocking/ranged flags, garrison, caps, wire
M4  Enemy AI: profiles (styles), warband battle state, flanking, siege
M5  AoE & alchemist: damage class, safe-arc over-throw, hold-fire, Lob, wall shelter
M6  Formations: shapes + rotation + persistent modifier + gate degradation
M7  Client battle experience: full selection/camera/minimap, command card, facing/structures/
    AoE visuals, pending-damage ledger, procedural sprites, touch modes
M8  Client mission flow: mission pick → roster select → deploy placement → results
M9  Entry & integration: combat activity entry from the hub/game flow
M10 QA & close: determinism harness, balance pass, docs close, linter parity
M11 Platinum + cosmetics server: separate currency ledger, cosmetics.json catalog
    (whitelist-validated), character ownership/loadout, store/equip REST, idempotent
    purchases, rewards.json cosmetics seam
M12 Client cosmetics: store + loadout UI, procedural appearance rendering (6 slots +
    flourishes), welcome/roster cosmetic loadout on the wire
M13 Monetization QA & close: entitlement + idempotency smoke, no-pay-to-win whitelist
    proof, docs close; adventure-access seam reserved (no implementation)
```

Dependencies:
- M0 → all
- M1 → M2–M6
- M2 → M8/M9
- M3 → M4 (siege), M5 (wall shelter)
- M3+M5 → M7 visuals
- M2+M3+M5 → M7/M8 gameplay completeness
- M10 → M11 (stable wire/schema before layering store data on it)
- M11 → M12 (client store/loadout UI needs the server surface)
- M12 → M13

Client wire DTO changes that must track server wire changes (`facing`,
`structures`, event `result`, deploy phase) are folded into the milestone that
introduces the server change — the protocol client stays minimal but compiling
at every step.

---

## 2. Milestone 0 — Baseline: docs, combatant schema, config skeletons, linter

**Objective:** make the config surface + linter + docs agree with the design
before any sim code, so later milestones only author content.

**Design anchors:** §5.1, §5.4, §17.2; Appendix A decision log (D4, D11, D29–32)
and the tuning list.

### Doc-conflict list execution

- `docs/combat_rulesets.md` — skirmish: `win_conditions` → `warband_wiped`;
  `match_duration_seconds` documented as the safety cap (draw at cap unless the
  warband is already wiped); note leaver = defeat, draw stamps neither morale
  timestamp.
- `docs/combat_protocol.md` — add per §17.1: `facing` on unit rows, a
  `structures` array + `structure_spawned/_built/_destroyed`, `attack` event
  `result` (`landed | dodged | blocked` + blocking structure id), `deploy`
  phase, `deploy_zone` on maps, `match_ended` xp/rewards payload.
- `docs/combat_maps.md` — per-obstacle blocking flags (`blocks_movement`,
  `blocks_ranged`, `blocks_aoe`); `deploy_zone` per team.
- `server/docs/combat_system.md` — combatant config new fields.
- `server/tables/retinue_members.md` — `xp` column documented now (C14/M2).

### Combatant config schema (both files)

`game/config/player_combatants.json` + `game/config/enemy_combatants.json` add
**optional** fields, all with defaults so existing entries still load:

| Field | Shape | Default |
|---|---|---|
| `max_hp` | per-level array | class default |
| `attack_range` | per-level array | class default |
| `attack_interval` | per-level array | class default |
| `attack_type` | `melee` \| `ranged` \| `magical` | highest damage stat (D5) |
| `direction_defense` | `{front, side, back}` | `{1.0, 0.6, 0.35}` |
| `dodge` | `{front, side, back}` | `{0, 0, 0}` |
| `turn_rate` | number > 0 | global constant |

Per-level arrays follow the existing `damage`/`defense` length and
interpolation/extrapolation conventions with single-element guards. Author
shape-correct values per class (knight `{1.0, 0.6, 0.3}`, goblin
`{0.9, 0.85, 0.85}`, etc.). Numbers are balanced in M10.

### New config skeletons (valid from day one, tuned in M10)

- `game/config/combat/warbands.json` — `opportunities[]`:
  `{id, name, tier, ruleset, map, warband[], xp_base, flavor}`; warband
  entries `{unit_class, level, count, group?}` (group for AI authoring in M4).
- `game/config/combat/structures.json` — barricade / spikes / wall / tower:
  footprint, hp, build_time, per-player caps, `blocks_ranged`,
  `blocks_physical_ranged`/`blocks_magical_ranged`, garrison slot (+damage/
  +range buff), sprite refs.
- `game/config/combat/formations.json` — line/column/wedge/circle offset
  tables + rotation + movement params.
- `game/config/combat/profiles.json` — per-class fight styles + per-group
  `tactic`/`aggro_model`/`engagement`/`retreat`/`siege`.
- `game/config/combat/rewards.json` — `xp_curve`, `victory_multiplier`,
  `draw_xp` (base only), empty `gold`/`equipment` tables (design D22).

### Ruleset + maps

- `game/config/combat/rulesets.json` — skirmish: `win_conditions:
  ["warband_wiped"]` (C1).
- `game/config/combat/maps/meadow.json` — add per-obstacle blocking flags and
  placeholder `deploy_zone` entries (real zones authored in M2).

### Linter (`tools/check_configs.py`)

- Combatants: validate the new optional fields (types/enums/range; per-level
  arrays; triple shapes; `turn_rate > 0`).
- New validators for `combat/warbands.json`, `structures.json`,
  `formations.json`, `profiles.json`, `rewards.json` (register in the config
  dispatch section, ~line 4659).
- `combat/maps/*.json`: validate `deploy_zone` shape and obstacle flag keys;
  extend the existing map validator.

### GameConfigCache (`server/GameConfigCache.hpp/.cpp`)

- Getters following the `getCombatRulesets()` pattern:
  `getCombatWarbands()`, `getCombatStructures()`, `getCombatFormations()`,
  `getCombatProfiles()`, `getCombatRewards()`.

### Exit criteria

`./tools/check_configs.py` clean (no errors); `./compile_server.sh` passes;
all five combat-related docs updated; retinue table doc updated.

---

## 3. Milestone 1 — Simulation core

**Objective:** a real battlefield: stats, HP bridge, movement + collision,
acquisition/targeting, combat resolution with arcs + dodge, death/wounds,
seeded PRNG, and annihilation end conditions — with a **static** enemy warband
(no AI yet; enemies idle camping).

**Design anchors:** §5, §9 (Move/Attack/Stop/Hold/Stance subset), §10, §13.3,
§16.1/16.2; D8–13, 25, 27, 41; baked-in rulings (collision, attack `result`,
no mid-match level-ups).

### Server type/model — `server/combat/CombatTypes.hpp`

- `combat_unit` gains:
  - `team` directly on the unit (C2),
  - `facing` (radians),
  - negative-id namespace for enemies (C3),
  - a stats block (`max_hp`, `damage{melee,ranged,magical}`,
    `base_defense{...}`, `direction_defense`, `dodge`, `attack_range`,
    `attack_interval`, `attack_type`, `turn_rate`, `movement_speed`),
  - runtime state (`std::deque<combat_order>` order queue, `target_id`,
    `cooldown_remaining`, `stance`, `formation_tag`, waypoints).
- New `combat_order` struct (D28): `{type, target_point, target_unit_id,
  queued}`. Explicit orders replace the queue; the channel-cancels-on-explicit
  rule lands with M3.
- `combat_message` gains `std::vector<combat_structure> structures` (declared
  now, populated in M3) and unit-row wire includes `facing` (C18a).
- New `combat_match_config` snapshot struct: ruleset, map_metadata, warband
  (enemy spawn list), player + enemy combatant configs (JSON copies), rewards
  (C16).

### `server/combat/CombatMatch.hpp/.cpp`

- Constructor takes `combat_match_config`. A per-match `std::mt19937 rng_`
  seeded at construction (D27); `--combat-seed` CLI override for tests.
- **Spawn:** players deploy at the team deploy/spawn points; warband units
  spawn near the team-2 camp area with negative ids, owner 0, team 2. Stats
  resolved from the config snapshot — no locks on the sim thread (C10, C16).
- **HP bridge (C6):** extend `retinue_member_snapshot` with `health`
  (0..100) + `fieldable`; `max_hp = class_max_hp(level) × health/100`;
  the fieldable filter stays at snapshot-build time.
- **Movement (C7):** base speed 0.04 × class `movement_speed`; waypoint
  following over the `tile_costs` grid (grid path first; full A* / flow fields
  fold into M4). Hard-body collision resolve: a grid-hash nudge pass keeps unit
  circles from overlapping (uniform radius per class, 2–3 iterations);
  structures are treated as impassable once they exist (M3).
- **Facing:** movement heading while moving; rotate toward target at `turn_rate`
  (per unit, δt-corrected).
- **Combat resolution (§5.5):** per-unit cooldown on `attack_interval`; on
  fire: arc lookup (facing vs target direction — front ±90°, sides 2×45°, back
  ±45°), `effective_defense = base_defense[type][level] × direction_defense[arc]`,
  `final = damage × 100 / (100 + effective_defense)`, then a dodge roll against
  the defender's arc value (seeded RNG). Ranged LOS: unit bodies never block
  (D45); structures block (M3).
- **Acquisition (§10):** stances `aggressive` / `defensive` / `hold`; Move never
  auto-engages (D41); retarget on target death/disengage; attack-move-to-gain-LOS
  behavior.
- **Events:** `attack` events carry `result: landed|dodged|blocked` (+ blocking
  structure id), damage, arc landed on; `unit_spawned / _died / _retreated`.
- **End conditions (§3.2/3.4, C1):** players wiped → defeat (0); warband wiped →
  victory (1); safety cap without warband wipe → draw (−1). PvE winner mapping
  per §13.3.
- **Death/casualty:** `wounded` mode → `casualty_record{member_id, owner,
  end_hp}` for later persistence (M2); enemy deaths are purely in-match.

### `CombatMatchManager` + REST wiring (`server/main.cpp`, `ApiHandlers.hpp`)

- `create_pve_match` / `join_match` build the `combat_match_config` snapshot on
  the loop thread from `GameConfigCache` (C16).
- `handleCombatCreate` temporarily keeps ruleset+map for M1 (switches to the
  opportunity in M2).

### Server docs

`docs/combat_protocol.md` — the full §17.1 delta in prose (facing, events,
phase enum note; `structures` declared for M3).

### Exit criteria

`./compile_server.sh`; `agent_test_server.sh -N 25`; a WS smoke script
(`tools/combat_smoke.sh` via wscat or a tiny companion client): create → join →
ready → countdown → battle; move your squad onto the idle enemies; observe
damage/dodge/arc events, deaths, victory on wipe, draw at cap. Determinism
spot-check: same seed + same commands → identical log.

---

## 4. Milestone 2 — Mission model, deploy, persistence

**Objective:** the opportunity system, roster choice, the deploy phase, and
conclusion persistence (XP / morale / draw / forfeit).

**Design anchors:** §3.1–3.5, §4.1–4.6, §13.1–13.3, §14; D19/20, 62–66;
baked-in rulings (deploy facing, draw morale, knight always fielded).

### Config

- Author `combat/warbands.json` for the v1 set (confirmed D7): `goblin_raiders`,
  `orc_war_party`, `bandit_stronghold`, `troll_march` (tiers 1–4, escalating
  composition).
- Maps gain real per-team `deploy_zone` regions — normalized rects
  `{team, x, y, w, h}` with a spawn-point-radius fallback (D3).
- `rewards.json` gets concrete `xp_curve` + `victory_multiplier` + `draw_xp`
  (D2 — numbers tuned in M10).

### Server

- **Opportunities surface (C20):**
  - `combatGetConfigs` returns `opportunities[]` (id, name, tier, ruleset, map,
    warband preview, flavor).
  - `combatCreate` accepts `opportunity_id` (+ optional `unit_ids` roster
    subset); the warband is locked at create (D62).
  - `combatList` summaries gain tier/opportunity fields.
- **Roster select (C20, D4):** snapshot flags `fieldable`; `combatCreate`'s
  optional `unit_ids` filters the host snapshot; joiners select via a new WS
  `roster` command while in the lobby (allowed until ready); the knight cannot
  be deselected (D19, baked-in ruling).
- **Deploy phase (C4, C5):**
  - `match_phase::deploy`; lobby → deploy once all players have sent rosters and
    `ready`.
  - Placement via WS `command {type: "deploy", unit_ids, x, y, facing}` —
    validated inside the player's team deploy zone and non-overlapping.
  - Default facing = toward the opposing camp.
  - Per-player `ready` early-ejects when all ready; otherwise auto-deploy on a
    ~25 s timer (default formation at the zone, facing camp); then countdown →
    battle. Spawn moves from countdown-start to deploy-end.
- **Conclusion persistence (C14/15):**
  - XP: per-fielded-member base from the opportunity's `xp_base` (warband-derived;
    never divided by roster/party size — D63); victory × `victory_multiplier`;
    draw = base only (D66).
  - New `retinue_members.xp` column (migration following the `honor_name`
    migration pattern) + `retinue_db::grant_experience(db, char, {member: xps})`
    computing level-ups against class `max_level` (D43: knight levels
    independently). Applied via `Loop::defer` only after persistence — no
    mid-match level-ups.
  - Morale: win/loss stamp `last_victory_ts`/`last_defeat_ts`; draw stamps
    neither; forfeit (leave) = defeat (C15, D66, §3.5).
  - `match_ended` payload: per-player `{xp_by_member, level_ups, wounds,
    winner_team, reason, rewards_stub}`.
  - Leaver semantics (§3.5): units withdraw **unwounded** (friendly removal, no
    casualty records); structures (M3) persist team-owned; garrison eject (M3).

### Client

- Wire DTOs: `combat_state_payload.phase` gains `deploy`;
  `combat_welcome_payload` gains opportunity + deploy zones;
  `combat_ended_payload` gains the XP table (UI built in M8).
- Minimal deploy placeholder (click squad → click zone to place → press Ready)
  so the loop is testable end-to-end; full polish in M8.

### API docs

`api/combatCreate.md`, `api/combatGetConfigs.md`, `api/combatJoin.md`
re-documented (opportunity + roster params; new payload fields).

### Exit criteria

Full PvE loop server-side: opportunity → create → join → roster → deploy →
battle → results with XP/morale correctly persisted; draw at cap stamps neither;
forfeit = defeat; `check_configs.py` clean.

---

## 5. Milestone 3 — Temporary structures

**Objective:** channeled construction, physical blocking, ranged-block
properties, garrison, caps — server + wire.

**Design anchors:** §7; §5.7 (units never block; structures do); §6.4 property
pair; D34–38, 45–46, 56, 58–61; baked-in rulings (solid immediately,
multi-builder, any explicit order cancels channel, garrison death leaves tower
empty, expel on destroy).

### Config

`combat/structures.json` values: barricade (fire-through), spikes (budget —
cheaper/faster/lower HP), wall (true block + AoE shelter), tower (arrow-slit:
movement blocked, ranged in *and* out; garrison 1; +damage/+range buff);
per-type `max_per_player` caps (D37; tuning per D59).

### Server (`CombatTypes.hpp`, `CombatMatch.cpp`, wire)

- `combat_structure`: id, owner, team, type, x/y, footprint, hp/max_hp, state
  (`building | active | destroyed`), garrisoned_unit_id, block flags.
- **Build:** WS `command {type:"construct", unit_ids, structure_type, x, y}`;
  builders path to the site and channel (busy + vulnerable); up to N (≈2)
  builders add build rate; HP ramps from 0 and damage persists; 0 HP mid-build =
  destroyed + builders freed; **solid/impassable immediately** (D58); placement
  validation (open tile, no unit/structure/obstacle overlap, tile-cost gate,
  per-player cap, no team lock — D37); any new explicit order cancels the
  channel (baked-in ruling).
- **Blocking:** `blocks_ranged` / `blocks_physical_ranged` /
  `blocks_magical_ranged` honored in ranged LOS and AoE shelter (M5); arrows
  resolve `blocked` with the structure id.
- **Garrison (§7.4):** tower capacity 1; moving an archer onto the tower
  auto-garrisons; melee-immunity (melee cannot target), ranged/magical can hit
  the archer directly; config buffs applied; tower destroyed → archer expelled
  at current HP at the tower footprint; garrison death → tower empty/non-buffing
  and re-garrisonable; building-state towers can't garrison.
- **Forfeit integration:** a leaver's structures persist as team-owned;
  garrisoned archers eject unwounded.
- **Wire:** `structures` array (full + dirty deltas), `structure_spawned /
  _built / _destroyed` events; JSON codec `append_structure_row`; destruction
  enqueues removal alongside unit removal.

### Client (protocol only, minimal)

- `combat_structure_dto`, the structures array in `combat_state_payload`, and
  structure events; `CombatGame` renders simple filled squares + HP/build bars
  (full treatment in M7).

### Docs

`docs/combat_protocol.md` structures section finalized (`combat_structure`
wire schema, events, codec rows). No new SQL table — structures are RAM-only.

### Exit criteria

Build a barricade mid-battle via WS; units path around it; arrows stop at a
wall (`blocked` result); garrison an archer (buff active, melee can't hit,
ranged can); destroy the tower → expel; caps enforced; forfeit keeps structures.

---

## 6. Milestone 4 — Enemy AI

**Objective:** warbands fight like warbands — the profile system, battle state,
flanking, siege.

**Design anchors:** §11, §12.5 (reason icons), D6/33; battle-state rulings.

### Config

`combat/profiles.json`:
- Per-class styles: goblin swarm/surround, bandit skirmisher/flee-and-
  reposition, orc stout/focus-fire/retreat-at-low-HP, troll juggernaut,
  dark mage backline/retreat-when-charged, flask-lobber artillery (its safe-arc
  mechanic finishes in M5).
- Warband `groups[]` with `tactic` (`charge|hold|skirmish|guard|reinforce`),
  `aggro_model` (`local|call_to_arms|global`), `engagement` (backline priority,
  ranged spacing, focus-fire), `retreat` threshold, `siege` flag.

### Server (`CombatMatch.cpp`)

- **Battle state (§11.3):** first contact flips the warband to wartime; idle
  groups adopt their configured wartime behavior; `call_to_arms`/`global`
  propagate alerts so pick-offs pull reinforcements.
- **Group movement (§8.1):** shared **flow fields** over `tile_costs` toward the
  goal (nearest player squad centroid or the camp alarm point); player units
  keep per-unit **A*** with lazy replan on stuck detection.
- **Per-style behavior:** surround-to-expose-backs (goblin), ranged spacing +
  flee (bandit), focus-fire + retreat threshold (orc), priority-magnet (troll),
  backline + flee-when-charged (dark mage), siege attacks when a wall fully
  blocks a group's approach (uses M3 flags).
- **Flank synergy (§11.4):** surrounding goblins open lanes for the lobber's
  safe centers (shared geometry function, built in M5).
- Targeting falls out of arcs naturally — attacks land on exposed arcs.

### Docs

`docs/combat_rulesets.md` + `server/docs/combat_system.md` — profile/behavior
schema.

### Exit criteria

Goblins split and surround (rear-arc hits visible); orcs present fronts and
focus-fire; reinforcing groups arrive on contact; walled-up players make siege
groups attack walls; bandits flee melee.

---

## 7. Milestone 5 — AoE & the alchemist

**Objective:** the indirect-fire layer, in both directions.

**Design anchors:** §6; D47–53, 56–57; baked-in rulings (no dodge in the AoE
path, attacker's own body counts as friendly for over-throw, over-throw cap ≤
splash radius bounded by max range).

### Config

- `player_combatants.json` + `enemy_combatants.json`: `alchemist`
  (splash-only, ≈2.9 s interval, short–mid range, splash ≈1.5 tiles = a 3×3
  man-grid ≈ 0.09 normalized) and the enemy `flask_lobber`.
- Splash resolves vs **magical defense**; ignores arcs; no dodge.

### Server

- **AoE damage class (§6.1):** uniform damage to every unit + structure in
  radius (including building-state structures) with flat mitigation; arc- and
  dodge-less.
- **Wall shelter:** a `blocks_ranged` wall intersecting the epicenter→target
  line shelters that target; fire-through (barricade/spikes) does not shelter;
  natural terrain obstacles follow per-obstacle flags (§8.4).
- **Safe-center over-throw (§6.3.1):** auto-acquisition seeks a landing center
  clearing every friendly (units **and** friendly structures; the attacker's own
  body counts — no self-splash); fire-through cover is **cleared** (cover-lob);
  walls/towers stay protected (D57); shift ≤ splash radius, bounded by max
  range.
- **Hold fire (§6.3.2):** no safe center → the unit holds fire and emits a
  `hold_fire` status event with a reason until a safe arc exists.
- **Lob (D51):** Attack accepts a ground point for AoE units (`attack` with
  `ground_x/y`); the flask lands exactly there; friendly-fire is the player's
  choice.
- **Enemy lobber (§11.5):** the same safe-center logic against its own warband's
  units; presents the wall-shelter counter to co-op parties.
- **Events:** AoE attack events carry center + radius + a per-target damage
  list (client ledger in M7).

### Docs

`server/docs/combat_system.md` — AoE + arc-less rules.

### Exit criteria

Kite an alchemist into auto-fire (no self-splash); spread formations shrug off
3×3; walls shelter; explicit bypass (`Lob`) is target-precise; no-safe-shot →
hold-fire reason event; enemy lobber clears its own warband's units.

---

## 8. Milestone 6 — Formations

**Objective:** persistent formation movement with degradation.

**Design anchors:** §8.3; §9 (formation order); D23, 42.

### Config

`combat/formations.json`: line/column/wedge/circle offset tables
(anchor-relative), rotation support, movement params.

### Server

- `combat_unit.formation_tag` + a match-level formation registry per
  player/team (anchor + shape + rotation).
- A formation stays active (shaping subsequent Move / Attack-Move into
  per-unit offsets) until toggled off or a non-formation order (D42).
- Units face travel direction and reform while moving.
- **Gate degradation:** narrow-passage detection → units degrade to free
  pathing, then reform at the destination (no hard-body deadlock, D42).
- Hotkeys are client-side; the server only tracks the current formation.

### Exit criteria

Order a Move in wedge/line/circle and hold the pattern; rotate the handle;
a chokepoint degrades then reforms cleanly.

---

## 9. Milestone 7 — Client battle experience

**Objective:** make the battle legible and controllable per §12.

**Design anchors:** §12; §5.4 (arcs in UI); §12.5 feedback; D14–18, 23;
pending-damage ledger (D8).

### `client/src/combat/protocol.ts`

Settle DTOs: `facing` on units; `structures`; event `result` types; deploy
phase; ended payload XP.

### `CombatGame.svelte` (major)

- **Rendering:** procedural class sprites (team color + class silhouette)
  replacing the circle placeholders; **facing chevrons**; selection rings; HP
  bars; name labels; **damage numbers timed to arrow landing** via a
  pending-damage ledger fed by attack events (HP change applied when the visual
  lands, reconciled to server HP at the next full snapshot) (D8, §12.5);
  hold-fire reason icon; friendly-fire highlight on risky explicit shots (blast
  circle + caught friendlies glow).
- **Selection (§12.2):** click, drag-box, shift/ctrl add, double-click
  type-select, control groups Ctrl+1..9 (touch: long-press assign + card
  button).
- **Camera (§12.3):** pan, edge scroll (toggleable), wheel zoom (clamped),
  WASD/arrows, minimap click-jump.
- **Pointer (§12.1):** LMB select/box, RMB context command (move/attack/Lob),
  MMB or M2-drag pan.

### New `CombatCommandBar` (replaces the thin HUD)

- Selection info: name, class, level, HP, **front/side/back defense & dodge**.
- Order buttons per §9/§12.4: Move / Attack (→ Lob for AoE units) / Stop /
  Hold / Patrol / Stance (`aggressive | defensive | hold`) / Construct
  (structure palette when builders are selected) / Formation (palette +
  rotation).
- **Queue-toggle pill** (touch parity, §12.6); shift-click queue on desktop.
- Deploy-phase bar (place squads, rotate facing, Ready) — polished from M2's
  placeholder.

### Touch modes

- **Smart-tap** vs **context-panel** mode toggle (D17); long-press-drag pan,
  pinch zoom, tap-minimap, on-screen drag = box select; both modes keep the
  command bar visible.

### Text system

All new strings in `game/text/en/*.txt` (`ui_`-prefixed UI ids, e.g.
`combat_cmd_move`, `combat_cmd_lob`, `combat_stance_aggressive`,
`combat_form_wedge`, `combat_touch_mode_smart`,
`combat_hold_reason_no_safe_arc`) plus their class/per-file `.meta` English
context; rendered via `GameText` / `loadTexts` — **zero hardcoded strings**.

### Exit criteria

`npm run check` passes (pre-existing WeedingGame errors only); a live battle
shows sprite armies with facing chevrons, wall/build visuals, AoE rings with
friendly-fire highlight, arrow-synced damage numbers, full selection/camera,
and a working command card incl. Lob + Construct + formation hotkeys.

---

## 10. Milestone 8 — Client mission flow

**Objective:** the §4 journey end-to-end in the UI.

**Design anchors:** §4.1–4.6; D19–20.

### Components

- **MissionPick** — opportunity browse: tier, map, warband preview
  (composition + flavor), locked-warband note.
- **RosterSelect** — fieldable toggles (default all; knight locked) shown after
  mission pick (§4.2).
- **MatchLobby rework** — create (opportunity + roster) / join by code; joiners
  roster-select in the lobby; per-player ready; host shares the code.
- **DeployView** — drag squads inside the team deploy zone, rotate facing
  (default toward the enemy camp), per-player Ready / auto-deploy timer.
- **ResultsView** — victory/defeat/draw banner + reason; per-member XP gained +
  level-ups; wounds list; rewards stub (morale now; gold/equipment tables empty).

### Text system

New strings for mission pick / roster / deploy / results (warband flavor, tier
labels, XP/level-up rows, wounds).

### API client (`client/src/lib/api.ts`)

- `combatGetConfigs` → opportunities; `combatCreateRequest(opportunity,
  roster)`; `combatJoinRequest`; the WS `roster` command added to
  `CombatNetClient`.

### Exit criteria

Full journey in-browser: hub → mission pick → roster → lobby → deploy →
countdown → battle → results reflecting actual XP/wounds; Back/redirect hygiene
per the hash-router rules.

---

## 11. Milestone 9 — Entry & integration

**Objective:** make the combat game reachable from the app flow.

**Design anchors:** §4 (journey start), §13.

- **Confirmed D1:** introduce a new `combat` hub activity card
  (`activity_combat` text id) gated behind the `land_patent` game phase
  (a manor + retinue exist). Integrate `App.svelte` routing → `CombatScreen`
  (`#/activity/combat/...`), reusing the existing activity-card pattern; scope
  the `mode` prop carving ('pve' only for v1).
- Reintroduce the lobby browser (`combatList`) into the MissionPick screen
  (open opportunities visible; join by code).
- Update `client/README.md` (entry points) + `AGENTS.md` endpoint/activity
  lists.

### Exit criteria

From a real save (post `land_patent`), a player can enter combat, run a full
match, and return to the hub with persisted XP/morale.

---

## 12. Milestone 10 — QA, determinism, balance, docs close

- **Determinism harness (D27):** `--combat-seed` CLI + `tools/replay_combat`
  (matches write seed + command log to `combat_replays/`; the replay tool
  re-runs and compares the outcome/state hash). Verify same seed → identical
  outcome/log.
- **Config linter parity:** any config change runs `tools/check_configs.py`;
  new-field validations stay in sync; AGENTS.md **Config File Changes Rule**
  followed (docs update alongside every config change).
- **Balance pass (D2/D6):** design-appendix tuning constants (base speed,
  splash radius, caps, XP curve, build times, defenses, arc multipliers) get a
  first real pass against playtests; numbers documented in
  `server/docs/combat_system.md`.
- **Docs close:** verify every `rts_design.md` section maps to a delivered
  file; update `AGENTS.md` (endpoints, configs, workspace progress),
  `client/README.md`, `server/README.md`.
- **Regression:** full `agent_test_server.sh` sweep + client `npm run check`.

---

## 13. Milestone 11 — Platinum & cosmetics server

**Objective:** ship the premium-variety machine (D67–78): a separate Platinum ledger, a
whitelist-validated cosmetic catalog, character-bound ownership + loadout, and the store
REST surface — with no gameplay key reachable by the simulation.

**Design anchors:** §15.0–§15.6; D67–78.

### Config
- `game/config/cosmetics.json` (new) — catalog `{ format_version, items[], bundles[] }`.
  Six slots; tiers `common|uncommon|rare|legendary`; `price_platinum` (non-negative int);
  `source` `store|adventure_reward`; `appearance` descriptor. A small stock set per slot so
  the store is never empty.
- `tools/check_configs.py` — `validate_cosmetics`: **deny-by-default whitelist** (only
  `id, name, slot, tier, price_platinum, source, description, appearance, sort`); appearance
  keys limited to `palette, pattern, emblem, trim`; non-negative price; unique ids; valid
  slot/tier; **any unknown key → ERROR** (the no-pay-to-win proof).
- `game/config/combat/rewards.json` — add an empty `cosmetics` array to the pipeline (the
  adventure-reward seam, §15.6).

### Server
- DB (game.db, idempotent migrations in `init_db.cpp`):
  - `platinum_ledger (character_id PK, balance INTEGER NOT NULL DEFAULT 0)`
  - `platinum_transactions (id, character_id, product_id, platinum, status, idempotency_key UNIQUE, created_at)`
  - `character_cosmetics (id, character_id, cosmetic_id, source, acquired_at, revoked)`
  - `character_loadout (character_id PK, loadout TEXT JSON)`
- `GameConfigCache::getCosmetics()` (+ `initialize` registration, hot-reload).
- REST (`server/main.cpp`, documented under `api/`):
  - `getStore` — catalog + balance (auth).
  - `getCosmetics` — owned set + loadout (auth).
  - `purchasePremiumItem {item_id, idempotency_key}` — server-authoritative: check
    balance/duplicate, debit, grant, record transaction (exactly-once via the unique key).
  - `purchasePlatinum {product_id}` — gateway **stub**: records an intent/transaction;
    settlement deferred.
  - `equipCosmetic {slot, cosmetic_id}` / `deassignCosmetic {slot}` — ownership-checked.
- No sim read path: the catalog's `appearance` is only ever copied into the welcome
  snapshot (M12); the simulation never consults cosmetics.

### Docs
- New `api/getStore.md`, `api/getCosmetics.md`, `api/purchasePremiumItem.md`,
  `api/purchasePlatinum.md`, `api/equipCosmetic.md`.
- `server/README.md` — premium section; `server/tables/*.md` for the four new tables.

### Exit criteria
- `check_configs.py` rejects a cosmetic carrying any gameplay key (prove with a temp edit).
- Purchase is idempotent: the same `idempotency_key` yields exactly one grant.
- `getStore`/`getCosmetics` round-trip; equip/deassign validates ownership.
- Server compiles; linter clean.

---

## 14. Milestone 12 — Client cosmetics

**Objective:** store + loadout UI and cosmetic rendering through the sprite API, plus the
welcome/roster cosmetic loadout on the wire.

**Design anchors:** §15.2–§15.3, §15.5.2; D69–71, D77.

### `client/src/combat/protocol.ts`
- `combat_welcome_payload` gains `banner`; `combat_retinue_member_dto` gains `cosmetic`
  (the member's slot→item loadout).

### Components & rendering
- Store + loadout panel (reachable from the combat activity; hub-wide later): browse by
  slot, buy with Platinum, equip/deassign; owned vs locked states.
- `CombatGame.svelte`: render each unit/structure from its appearance descriptor
  (palette/pattern/emblem) while **always** overlaying the team ring + HP bar + facing
  chevron (no-camouflage, D77); flourish tints on `attack`/AoE/victory/death events.
  Procedural first, art behind the same API (D71).
- `client/src/lib/api.ts` — store/cosmetics requests.
- Text system — store/loadout/flourish strings (no hardcoded text).

### Exit criteria
- Buying, equipping, and deassigning round-trips and persists across reload.
- Equipped cosmetics visibly change the battle (procedural) without altering the
  team/HP/facing overlays.
- `npm run check` clean (combat-only).

---

## 15. Milestone 13 — Monetization QA & close

- **Entitlement/idempotency regression** (`tools/monetization_smoke.*`): purchase once,
  replay the same key, assert one debit / one grant; a refund revokes and clears the
  loadout.
- **No-pay-to-win proof:** the linter whitelist rejects a seeded gameplay key; the match
  snapshot carries only `appearance`.
- **Docs close:** AGENTS.md config list + endpoint overview + progress; server README.
- **Adventure-access seam** documented as deferred (§18); no implementation.

---

## 16. Open decisions (resolved vs. authoring-time)

**Resolved during planning:**
- **D1 — Entry & gating:** new `combat` hub activity card gated behind
  `land_patent`.
- **D2 / D6 / D7 — Numbers & content:** shape-now/tune-later; v1 opportunity
  set = 4 (goblin/orc/bandit/troll, tiers 1–4).
- **D67–D78 — Cosmetics & monetization:** Platinum is a separate, non-fungible,
  variety-only currency; character-bound cosmetics; six slots; join-time snapshot
  delivery; procedural appearance; server-authoritative idempotent purchases;
  pluggable payment stub; no loot boxes; store ≠ adventure catalog; inert-by-construction
  whitelist; no camouflage; refunds revoke. Adventure *access* is deferred.

**Authoring-time defaults (no further sign-off needed):**
- **D4 — Roster timing:** host picks at mission pick; joiners pick in the lobby
  before ready.
- **D5 — `attack_type`:** derived from the highest damage stat; explicit value
  overrides.
- **D8 — Arc/dodge scope:** class-level v1; per-member overrides deferred.

---

## 17. Verification cheatsheet

Per-milestone gates:
1. `./compile_server.sh` — server compiles.
2. `./tools/check_configs.py` — all configs valid (after every config edit).
3. `./agent_test_server.sh -N 30` — REST endpoints under load.
4. `npm run check` in `client/` — client type-checks (WeedingGame
   pre-existing errors only).
5. Milestone-specific WS smoke tests as listed per milestone.
6. **Monetization gates (M11+):** a cosmetic with any gameplay key fails
   `check_configs.py`; the same `idempotency_key` grants exactly once; the match
   snapshot carries `appearance` only.

Every milestone must leave the repo compilable and lint-clean; partial
features are never committed behind stubs.