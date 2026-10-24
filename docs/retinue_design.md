# Retinue Design & Implementation Status

**Status:** v1 (design complete; Phase 1–2 + the generic money layer implemented; Phases 3–8 deferred)

## 1. Overview

The player is a knight with a sworn household (retinue) billeted at their manor. The
retinue feeds the realtime RTS game's roster: the knight is always present; additional
members are recruited, maintained, equipped, injured, healed, and dismissed within the
manor economy. **No permanent death anywhere** — the worst combat outcome is infirmary
time (availability loss, never roster loss).

Design pillars:
- **Economic pressure, not RNG** — maintenance is significant and configurable;
  recruitment variance is *level*, not luck.
- **Continuous health** — HP is a persistent resource recovered over wall-clock time,
  not a discrete injury-status model.
- **Knowledge is capital** — per-building tech trees make buildings specialized,
  long-term assets (deferred).
- **Many small links, no spirals** — morale connects the whole fiefdom, muted.

## 2. The Roster & the Cap

- **The knight** (`is_knight = 1`) is the character itself, auto-created. Always
  present, **free of base upkeep** (folded into the player; gear upkeep only),
  not dismissible, **does not consume a capacity slot**, and can be injured → locked
  from *RTS combat only* (queen-in-chess).
- **Capacity** = `retinue_capacity_by_level[manor_level]` in **`game/config/retinue.json`**
  *(deviation: the v0 design said economy.json; it lives in retinue.json for cohesion)*
  — 11 entries, counts **recruited** members only:

  | manor lvl | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | recruits | 0 | 2 | 3 | 5 | 7 | 9 | 11 | 14 | 17 | 20 | 24 |

- Members are **billeted in the manor house** — no per-member houses, no arable/forest
  acres claimed. Over-flow impossible (manor level only rises).

## 3. Recruitment Market  — ✅ implemented

- **Named candidates**, never a unit store: ~6 per unlocked class
  (`market.candidates_per_class`), uniform level `1..manor_level +
  max_candidate_level_offset` (a high manor still rolls level-1s).
- Class gating by `requires_manor_level` in `player_combatants.json` (spearman 1 /
  archer 1 / knight 3 / mage 4 — tunable).
- `Gender` is part of the offer (male **and** female pools); names come from
  `game/text/en/names_<class>_<gender>.txt`.
  *(deviation: pools are en-only today; per-language wiring is future.)*
- **Deterministic, never stored**: seeded from `(character_id, hour_bucket)` with a
  splitmix64 PRNG (`server/RetinueMarket.cpp`). Hour cohorts + a grace window
  (`grace_buckets = 1`): an offer from the previous hour stays hirable.
- **Identity (`class → sex → level → name → headshot`)**: name and headshot are
  rolled too and re-rolled deterministically past identities already in the
  character's roster (and past earlier offers in the same displayed list), so the
  UI never shows a duplicate. Headshots come from the sorted art pools at
  `game/images/retinue/headshots/{male,female}/` (no catalog config). Hire
  validation is **structural** (class/level/bucket + pool membership); exact-offer
  regeneration and the name-uniqueness gate are intentionally dropped.
- Hire fee = class `costs` at the candidate's level (linear extension);
  paid through the **generic money layer** (fungible gold⇄silver, auto-import of
  material shortfalls honoring `import_settings`) — same path buildings use.
- Endpoints: `listRecruitCandidates`, `hireRecruit`.

## 4. Member Data Model — ✅ schema / deferred logic

| Field | Purpose |
|---|---|
| `gender` | `male` \| `female` (name pool + future gender-aware prose) |
| `headshot_id` | Fixed identity portrait, a path under `images/retinue/headshots/` (e.g. `male/03.png`); empty for the knight, which resolves from the owner's PFP |
| `health` / `health_updated` | persistent HP 0..100, recovers over wall-clock time (continuous model) |
| `priority` | strict intra-roster rank — funding order + infirmary-bed order (player reorderable via `/api/setRetinuePriority`) |
| `equipment` | generic slot→item map (`weapon`, `armor`, `mount`, `potions`, ...) (deferred) |
| `abilities` | deferred until RTS mechanics land (playtesting) |

`status` collapsed: no `casualty` distinction going forward ("infirmary" == health
below the deploy threshold). Knight gender derives from `characters.sex`.

## 5. Maintenance & Funding — ✅ Phase 3 (economy tick)

- **Cost streams separated**: base upkeep (per class × level, config `upkeep` arrays —
  mandatory grain + gold wage + class materials) **and** equipment upkeep (per item,
  all members incl. the knight — Phase 5). Config-driven so unit types genuinely differ.
- **Provisioning**: manor production → stores → import (fungible money). The retinue
  is one economy budget line; inside it, **strict priority order** (all-or-nothing
  per member, `priority` ASC). The knight is free (excluded). Each member is funded
  via `money::affordable` / `money::pay` (shortfalls auto-imported with fungible
  money); `retinue_members.maintained` persists the per-member result.
- **Unfunded consequences**: per-class configurable effect (fighter: stat penalty;
  archer: cannot fight — Phase-4+ combat use) + general-morale penalty per member
  (`retinue.json morale.unfunded_member_penalty`, folded into the tick's `morale_damage`).
- GetFiefdom economy report gains a `retinue` section: `funded` / `unfunded` /
  `unfunded_members` / `gold_spent` / `pence_spent` (net gold/silver include it).

## 6. Injury, Recovery & the Infirmary — ✅ Phase 4

- **Severity = end-of-combat HP%** (no randomness). Health regenerates over
  wall-clock time from `health_updated`; `recovery_hours = ceil((100−hp%)/100 ×
  max_recovery_hours)`, max 16h (`retinue.json recovery`). Fielding locked below
  `min_deploy_hp` (default 50); at ≥50% you field at current HP (start the
  battle hurt); below → `fieldable: false` ("in the infirmary").
- **Infirmary** — new building chain `class: "infirmary"` (infirmary → surgery →
  hospital, stages gated at manor 1/5/9). Each completed stage adds a
  `recovery_multiplier` bonus (1.0/1.5/2.0 → up to 3× base healing), applied by
  the recovery engine. The three stages share `exclusive_group: "infirmary"`, so
  only one may exist at a time (their bonuses cannot stack; over-building is
  blocked by the server and hidden from the build palette). Optional: without it
  members heal at the base rate.
- **Combat**: skirmish rulesets use `death_handling: "wounded"` — fallen members
  are **wounded at their individual end-of-combat HP%** (the combat match records
  each unit's HP when it fell; the recovery clock restarts only if the health
  actually drops, so an already-worse injury isn't set back) via
  `retinue_db::apply_wounds()`; `apply_casualties` remains only for legacy
  `permanent` rulesets. **PvP scrimmage** (respawn) never touches the infirmary.
  Wins can still cost days. Potions = release valve (config items, future).
- **Deferred — infirmary bed management:** the `recovery` block has no capacity
  field today, so every wounded member recovers. If the infirmary can later hold
  fewer members than are wounded, an interface will be needed to choose who
  occupies beds; `retinue_members.priority` is already defined as the
  **infirmary-bed order** (as well as funding order), so the future UI can
  reorder priority or assign beds explicitly. Not implemented.

## 7. Equipment & the Tech Tree — ✅ (5a + 5b)

- **Generic tech trees** (`tech_trees.json`): trees of nodes with `effects`
  (start: `unlock_recipe`). Buildings declare `tech_trees` + per-level passive XP.
- **Per-building progression**: each instance stores `{xp, learned_nodes}` — two
  blacksmiths diverge (weaponsmith / armorer). Demolish wipes tech; `move` (existing
  10% action) preserves it.
- **XP sources**: passive accrual in the economy tick (`tech_xp_per_day`),
  training timers (teacher/book grants land on completion). One training/forge
  timer each per building; demolish mid-training loses the book.
  *(books/teachers endpoints: 5b)*
- **Gear model**: config-driven slots (armor, weapons, mount, potions),
  per-item `requires_level`/`allowed_classes`, daily upkeep, `armory_slots`, base value,
  craft/city-purchase data. **Basic kits** are class-inherent, invisible, zero-upkeep,
  fill empty slots automatically. **Equipped gear upkeep folds into maintenance**:
  the funding tick adds each equipped item's per-day upkeep to the member's cost.
- **Forge orders**: materials + timer, `max_concurrent_orders` (config, default 1),
  non-blocking (fun over realism). City purchases = extreme-markup bypass
  (`requires_manor_level`). Event rewards land unbidden (armory overflow policy).

## 8. Storage — ✅ Phase 5a (schema + read/write)

- **`fiefdom_armory`** (gear): slot-based; per-item `armory_slots`; finite but generous
  capacity (config). Equip/de-equip round-trips via member_id. Disposal = sell at
  `sell_discount`. Player-initiated adds blocked at capacity; **grants always land over
  cap** (must clear space before new crafting/purchases).
- **`fiefdom_storage`** (general items): count-based; books today, potions etc. later.
  Items carry category/source (`drop`/`purchase`)/typed effect (`grant_xp` +
  `training_duration`). Sell-at-discount; **future barony trade** reuses item identity +
  value (sell-to-town is the degenerate case).

## 9. Morale — ✅ Phase 6

- **Computed from state, never stored.** Household morale = Σ small
  config-weighted contributors: **chapel buildings** (`morale_boost`),
  funded members +small, unfunded −small, and recent battle win/loss
  **decay terms from stored timestamps** (`fiefdoms.last_victory_ts` /
  `last_defeat_ts`, set by combat `on_match_end`; decay over
  `morale.decay_hours`). Points → percent (`points_per_percent`) **clamped
  0..max_bonus_percent** — low morale earns no bonus but is never penalized
  below baseline (no doom-spiral; no hysteresis). Implemented in
  `Morale::computeHouseholdMorale` (`MoraleCalculator.cpp`).
- Effective per target = **general + individual** (road-network morale per
  building, the existing individual system, unchanged).
- Effects (each a small config coefficient): **building production** (the tick
  multiplies every output by `1 + general% / 100`), **recovery speed**
  (`morale.recovery_morale_bonus` of the percent feeds `getRetinue`'s recovery
  multiplier), exposed as `household_morale` in `getFiefdom`.
  **RTS unit morale** — deferred (combat mechanics are still a milestone).

## 10. Generic Money Layer — ✅ implemented

`server/Money.hpp/.cpp` is the single source of truth for "cost / available / pay":

- `money::currency` + `money::load_currency` (economy.json `currency` block).
- `money::wallet` (`gold`+`silver_pence` fungible) with `gold_equivalent`/`pence_equivalent`.
- `money::price_to_pence` / `price_to_gold` (money-object price entries).
- `money::take_pence` / `take_gold` (cross-currency payment), `add_pence`/`add_gold`.
- `money::affordable` / `money::pay` (`cost_context` of import prices + settings) —
  all-or-nothing, physical shortfalls auto-imported with money.
- **Callers**: `Validation::hasEnoughResources`/`deductResources` delegate to it;
  the economy tick's currency/pricing and import/export/gold-upkeep math routes through
  it; **hire fees** use the same fungible, auto-importing path.

## 11. Explicitly Deferred

Member **abilities** (playtesting); **PvE difficulty rating** on missions (party-power
estimate + UI hint); **barony trade**; **manor officials** (passive XP + effects);
**cavalry/mounts roster** (slot exists); **potion combat behavior** + alchemist chain;
analyzer tech-tree transparency (economic sim unaffected).

## 12. Implementation Status

| Phase | Status |
|---|---|
| 1–2 Config + schema + recruit market + roster | ✅ |
| Generic money layer (fungible gold ⇄ silver) | ✅ |
| 3 Maintenance/funding economy tick | ✅ |
| 4 Injury/recovery/infirmary + combat persistence (`wounded` ruleset) | ✅ |
| 5 Tech trees + gear + forge + storage endpoints | ✅ (5a core + 5b city purchase/teacher/book; client UI remains) |
| 6 Morale wiring | ✅ (production + recovery + getFiefdom; RTS unit morale deferred) |
| 7 Client Retinue panel + recruit UI | ✅ (Manor Retinue panel: management grid + recruit market) |
| 7b Priority reorder | ✅ (`/api/setRetinuePriority` + ↑/↓ controls in the member detail) |
| 7c Member identity (name + headshot) | ✅ (`headshot_id`, deterministic re-rolls, portrait rendering) |
| 7d Player PFP (safe picker + 18+ upload) | ✅ (safe picker in creation; Profile activity for safe/custom change + 18+ upload; age-gated serving) |
| 8 Tests (linter + endpoint smoke) | ⏳ |

## 13. Identity Portraits & Player PFP — ✅ implemented

- **Retinue headshots** are fixed member identity (`headshot_id`), assigned at
  hire and rendered inside a frame. Art lives at
  `game/images/retinue/headshots/{male,female}/<id>.<ext>` (png/jpg/jpeg/webp/avif),
  frames at `game/images/retinue/frames/{recruit,management}.png`. Headshot pools
  are scanned + sorted at startup (`server/AvatarPool.*`); no catalog config.
- **Frame geometry** is cataloged in `game/config/portrait_frames.json` and served
  by `/api/getPortraitFrames` (see `server/docs/portrait_frames.md`): each variant
  names its overlay `image`, its `image_width`/`image_height`, and the `portrait`
  rectangle (in the same pixels) the headshot is mapped into. The frame is drawn
  **on top** of the portrait (a transparent PNG, `pointer-events: none`), so the
  rectangle is a **draw box, not a mask** — the frame's opaque pixels hide
  whatever spills past the opening, which is what allows a non-square/ornamental
  interior edge. Pixel coordinates are used because they are easiest to measure;
  the client converts to fractions using the declared image size.
- **Rendering** (`RetinuePortrait.svelte`): the frame is contained into the
  component box preserving its aspect, and the portrait rect is mapped through
  the identical scale/offset (so non-square frames align). The headshot is
  **stretched to fill** the rectangle (`object-fit: fill`) so a portrait with a
  background never letterboxes; art authors keep headshots near-square. With no
  frame config (or a failed frame image) the portrait fills the box.
- **Player PFP** is per-character and variety-only, and is a **layered** identity
  mirroring the safe/custom name model. `avatar_safe_id` is the always-present
  safe pick (forced pick at creation, or skipped when the pool is empty — no
  random assignment), shown to minors and used as the fallback; `avatar_custom_id`
  is an optional 18+ upload layered on top for adult viewers. The upload is
  content-addressed (`<character_id>/<sha256>.<ext>`) and `avatar_storage` selects
  local (`uploads/avatars/`) or R2 (public assets bucket). Any change is
  rate-limited to once per 24h per character (`avatar_changed_at`); the creation
  pick is exempt.
- **The safe pool is gender-split** (`game/images/avatars/safe/{male,female}/`,
  scanned at startup like the headshots and **auto-rescanned when the pool
  directory's mtime changes** — new art appears without a restart).
  `/api/getAvatarPool` returns the pool
  for the requested `gender` (the character's sex, sent by the client) in a
  **per-user shuffled display order** (seeded from the username, so a player's
  order is stable across reloads while the same portraits are not always shown
  first; the canonical pool stays sorted). `/api/setCharacterAvatar` validates
  against the character's stored `sex`. Ids
  are gender-qualified (`male/1.png`); a one-time migration prefixes legacy bare
  ids with the owning character's sex.
- **URL resolution is shared** (`server/AvatarUrl.cpp`, `resolve_avatar_url`):
  the custom route is returned only when **both** the owner and the viewer are
  adults, else the safe pick. Every surface uses it — `getCharacter`, retinue
  knight portraits, and **chat** (`CharacterDirectory` / `search_characters`),
  which previously hardcoded the custom route for everyone.
- The hub **Profile** activity (`client/src/components/ProfilePanel.svelte`) is
  the change/upload UI: the safe grid, the 18+ upload (the client center-crops to
  256x256 and encodes WebP when the canvas supports it, else JPEG), remove-custom,
  and the cooldown. The safe layer is always editable; the custom layer is a
  separate overlay that `removeCustomAvatar` clears.
- **Age gating is server-side.** Safe art is public
  (`/images/avatars/safe/{male,female}/*`, scanned at startup and auto-rescanned
  when the pool directory changes — no restart needed);
  custom uploads live outside the static tree. The local backend is served only
  by the age-gated `GET /api/avatar/<character_id>` (owner or adult viewer); the
  R2 backend resolves to a public content-addressed capability URL. Under-18
  viewers always receive the owner's safe pick, and the custom URL is never
  disclosed to them.
- **RTS note:** the realtime game does NOT use these portraits. RTS player art is
  a separate, generic, directionally-animated system (with cosmetics), designed
  later; no combat DTO carries a portrait.
- **Moderation of custom uploads is a documented TODO.**