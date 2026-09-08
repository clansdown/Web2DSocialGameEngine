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
- Hire fee = class `costs` at the candidate's level (linear extension);
  paid through the **generic money layer** (fungible gold⇄silver, auto-import of
  material shortfalls honoring `import_settings`) — same path buildings use.
- Endpoints: `listRecruitCandidates`, `hireRecruit`.

## 4. Member Data Model — ✅ schema / deferred logic

| Field | Purpose |
|---|---|
| `gender` | `male` \| `female` (name pool + future gender-aware prose) |
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
  the recovery engine. Optional: without it members heal at the base rate.
  Over-building a second infirmary is simply bad value — players self-gate.
- **Combat**: skirmish rulesets use `death_handling: "wounded"` — fallen members
  are **wounded at their individual end-of-combat HP%** (the combat match records
  each unit's HP when it fell; the recovery clock restarts only if the health
  actually drops, so an already-worse injury isn't set back) via
  `retinue_db::apply_wounds()`; `apply_casualties` remains only for legacy
  `permanent` rulesets. **PvP scrimmage** (respawn) never touches the infirmary.
  Wins can still cost days. Potions = release valve (config items, future).

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
| 7 Client Retinue panel + recruit UI | ✅ (Manor Retinue panel: roster, market, gear) |
| 7b Priority reorder | ✅ (`/api/setRetinuePriority` + ↑/↓ controls in the panel's Roster tab) |
| 8 Tests (linter + endpoint smoke) | ⏳ |