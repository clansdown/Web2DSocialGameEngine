# Combat System Configuration

Configuration files:
- `server/config/damage_types.json` - List of damage types
- `server/config/player_combatants.json` - Player combatant definitions
- `server/config/enemy_combatants.json` - Enemy combatant definitions

All config files use lenient JSON parsing (allows comments, trailing commas) via nlohmann/json with `ignore_comments=true`.

**Minor-safe wire:** player and unit names in combat are safe names for every
viewer. Minor combat chat is safe-message IDs only; adult custom chat is
published only to adult teammates. Voice signaling is adult-to-adult only.
See `docs/minor_content_visibility.md` and `docs/combat_protocol.md`.

## TypeScript Type Definitions

```typescript
// --- Damage Stats per level ---
interface DamageStats {
    melee: number;
    ranged: number;
    magical: number;
}

// --- Defense Stats per level ---
interface DefenseStats {
    melee: number;
    ranged: number;
    magical: number;
}

// --- Cost Stats per level ---
interface CostStats {
    gold: number;
    grain: number;
    wood: number;
    steel: number;
    bronze: number;
    stone: number;
    leather: number;
}

// --- Combatant Definition ---
interface Combatant {
    // Required fields
    name: string;
    max_level: number;
    size: number;   // normalized body radius: sim collision + drawn sprite size

    // Arrays (index = level - 1, levels start at 1)
    // Empty array = 0 at all levels
    // 1 element = slope of 1 extrapolation
    // 2+ elements = linear extrapolation from last two
    damage: DamageStats[];
    defense: (DefenseStats | null)[];  // null = no defense at that level
    movement_speed: number[];
    costs: CostStats[];
    upkeep: CostStats[];  // per-day upkeep per stationed unit (level-indexed)

    // RTS combat fields (rts_design.md §5.1) — all OPTIONAL, with defaults
    max_hp?: number[];              // per-level in-match health pool
    attack_range?: number[];        // per-level engagement reach (normalized units)
    attack_interval?: number[];     // per-level seconds between attack resolutions
    attack_type?: 'melee' | 'ranged' | 'magical';  // default: highest damage stat
    turn_rate?: number;             // radians/sec rotate-toward-target (default: global constant)
    direction_defense?: { front: number; side: number; back: number };  // arc multipliers on the per-type base
    dodge?: { front: number; side: number; back: number };              // dodge chance per arc, 0..1
    aoe?: {                         // splash/AoE combatants (alchemist, flask lobber)
        splash: boolean;
        splash_radius: number;      // normalized units (~1.5 tiles ≈ 0.09)
        flat_mitigation: boolean;   // AoE ignores arcs & dodge; flat per-target mitigation
        safe_arc_auto_aim?: boolean;   // over-throw: clear friendlies, cap = splash radius
        hold_fire_on_no_safe_arc?: boolean;  // hold fire + status event when no safe shot
    };
}

// --- Config file format ---
type CombatantConfig = Record<string, Combatant>;  // Combatant ID -> Combatant
```

## Field Descriptions

### Combatant Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Display name (e.g., "Spearman", "Archer") |
| `max_level` | number | Maximum attainable level |
| `size` | number | **Required (RTS)** — body radius in normalized board units (0.03 = one standard soldier). Drives the sim collision radius / deploy spacing **and** the client's drawn sprite/token size, **independent of the source image dimensions**. A troll (0.065) reads far larger than a goblin (0.025). Validated `> 0` and `<= 0.2`. |
| `damage` | DamageStats[] | Damage output per level |
| `defense` | (DefenseStats \| null)[] | Damage reduction per level (null = no defense) |
| `movement_speed` | number[] | Movement speed per level |
| `costs` | CostStats[] | Resource costs per level |
| `upkeep` | CostStats[] | Per-day resource upkeep consumed by each stationed combatant per level (e.g. `{ "ironwork": 12 }`). Consumed each economy update, scaled by fractional elapsed days; shortfalls are imported/morale-penalized like other consumption. |
| `max_hp` | number[] | **Optional (RTS)** — per-level in-match health pool (HP bridge: `match_hp = max_hp × roster_health%`). Default: class-derived. |
| `attack_range` | number[] | **Optional (RTS)** — per-level engagement reach in normalized units. Default: class-derived. |
| `attack_interval` | number[] | **Optional (RTS)** — per-level seconds between attack resolutions. Default: class-derived. |
| `attack_type` | string | **Optional (RTS)** — `melee` \| `ranged` \| `magical`. Default: the combatant's highest damage stat (D5). |
| `turn_rate` | number | **Optional (RTS)** — radians/sec rotate-toward-target speed. Default: global constant. |
| `direction_defense` | object | **Optional (RTS)** — flat per-unit `{front, side, back}` arc multipliers over the per-level per-type defense base (default `{1.0, 0.6, 0.35}`). |
| `dodge` | object | **Optional (RTS)** — flat per-unit `{front, side, back}` dodge chance per arc, 0..1 (default all 0). |
| `aoe` | object | **Optional (RTS)** — splash/AoE combatant block: `splash`, `splash_radius`, `flat_mitigation`, `safe_arc_auto_aim`, `hold_fire_on_no_safe_arc`. Splash resolves vs magical defense, ignores arcs, no dodge (rts_design.md §6). |

### RTS combat model

For the realtime combat game (`/ws/combat`), the combatant config is consumed
per rts_design.md §5:

- **Arcs** (rts_design.md §5.4): front ±90°, sides 2×45°, back ±45° around the
  unit's `facing`. `effective_defense[type][arc] = defense[type][level] ×
  direction_defense[arc]`.
- **Mitigation** (§5.5): `final = damage × 100 / (100 + effective_defense)`
  (diminishing; no zero-damage hard walls).
- **Dodge** (§5.5): a separate roll against `dodge[arc]`; a dodged hit deals 0.
- **Movement** (§5.3): `movement_speed` is a multiplier on a config base speed
  of 0.04 normalized-units/s.
- **AoE** (§6): splash ignores arcs, no dodge, uniform to units + structures,
  resolves vs magical defense; true walls shelter (block the line to the
  epicenter); fire-through cover does not.
- **Terrain** (§8.4): `tile_costs` 0 blocks movement; cost > 1 scales movement
  speed down by that factor (rough ground is genuinely slower). Structures and
  movement-blocking obstacles are true pathing obstacles; units are not.
- **Enemy movement** (§8.1): each active warband group shares one **flow field**
  toward its group goal (camp for guard/hold, nearest player otherwise); units
  sample the field instead of each planning its own A*. Per-class styles
  (surround/keep-range/retreat) layer local steering on top.
- **Orders** (§9): `patrol` loops/ping-pongs between the unit's position and the
  target, engaging along the path; `attack_move` moves and engages en route;
  `move` never auto-engages. A `formation` order sets a persistent modifier.
- **Builds** (§7.2): a new explicit order cancels a unit's channel and **aborts
  the build, resetting its progress** to 0 (emits `build_aborted`); a
  builder's death does the same. The structure stays buildable.
- **Forfeit** (§3.5): a leaver's units **withdraw unwounded** (no casualty
  records, `unit_retreated`), garrisons empty, structures persist as team-owned;
  the leaver records a **defeat** and earns no XP regardless of team outcome.

### Damage Stats

| Field | Type | Description |
|-------|------|-------------|
| `melee` | number | Melee damage amount |
| `ranged` | number | Ranged damage amount |
| `magical` | number | Magical damage amount |

### Defense Stats

| Field | Type | Description |
|-------|------|-------------|
| `melee` | number | Melee damage reduction |
| `ranged` | number | Ranged damage reduction |
| `magical` | number | Magical damage reduction |

### Cost Stats

| Field | Type | Description |
|-------|------|-------------|
| `gold` | number | Gold cost |
| `grain` | number | Grain cost |
| `wood` | number | Wood cost |
| `steel` | number | Steel cost |
| `bronze` | number | Bronze cost |
| `stone` | number | Stone cost |
| `leather` | number | Leather cost |
| `charcoal` | number | Charcoal cost |
| `iron` | number | Iron cost |
| `ironwork` | number | Ironwork cost |

## Extrapolation Rules

All arrays (`damage`, `defense`, `movement_speed`, `costs`, `upkeep`) use the same extrapolation logic:

| Array Length | Behavior |
|--------------|----------|
| 0 elements | Default to 0 (or empty object) at all levels |
| 1 element | Linear extrapolation with slope of 1 |
| 2+ elements | Linear extrapolation from last two elements |

### Extrapolation Formula

For arrays with 2+ elements at index `i >= (length - 2)`:

```
value[i] = value[last] + (i - last) * delta
delta = value[last] - value[last-1]
```

### Example

Given `damage: [{ melee: 10 }, { melee: 15 }, { melee: 20 }]` with `max_level: 5`:

| Level | Melee Damage |
|-------|-------------|
| 1 | 10 |
| 2 | 15 |
| 3 | 20 |
| 4 | 25 |
| 5 | 30 |

## Config File Examples

### Damage Types

```json
[
    "melee",
    "ranged",
    "magical"
]
```

### Player Combatant Example

```json
{
    "spearman": {
        "name": "Spearman",
        "max_level": 10,
        "damage": [
            { "melee": 10, "ranged": 5, "magical": 0 },
            { "melee": 15, "ranged": 7, "magical": 0 },
            { "melee": 20, "ranged": 10, "magical": 0 }
        ],
        "defense": [
            { "melee": 5, "ranged": 3, "magical": 0 },
            null,
            { "melee": 10, "ranged": 5, "magical": 2 }
        ],
        "movement_speed": [1.0, 1.1, 1.2],
        "costs": [
            { "gold": 50, "steel": 10 },
            { "gold": 75, "steel": 15 },
            { "gold": 100, "steel": 20 }
        ]
    }
}
```

### Enemy Combatant Example

```json
{
    "goblin": {
        "name": "Goblin",
        "max_level": 5,
        "damage": [
            { "melee": 8, "ranged": 0, "magical": 0 },
            { "melee": 12, "ranged": 0, "magical": 0 }
        ],
        "defense": [
            { "melee": 2, "ranged": 1, "magical": 0 }
        ],
        "movement_speed": [1.5],
        "costs": []
    }
}
```

## C++ Data Structures

```cpp
namespace Combatants {

struct DamageStats {
    int melee = 0;
    int ranged = 0;
    int magical = 0;
};

struct DefenseStats {
    int melee = 0;
    int ranged = 0;
    int magical = 0;
};

struct CostStats {
    int gold = 0;
    int grain = 0;
    int wood = 0;
    int steel = 0;
    int bronze = 0;
    int stone = 0;
    int leather = 0;
};

struct Combatant {
    std::string id;
    std::string name;
    int max_level = 1;
    
    std::vector<DamageStats> damage;
    std::vector<std::optional<DefenseStats>> defense;
    std::vector<double> movement_speed;
    std::vector<CostStats> costs;
    
    DamageStats getDamage(int level) const;
    std::optional<DefenseStats> getDefense(int level) const;
    double getMovementSpeed(int level) const;
    CostStats getCosts(int level) const;
};

class CombatantRegistry {
public:
    static CombatantRegistry& getInstance();
    
    bool loadPlayerCombatants(const std::string& config_path);
    bool loadEnemyCombatants(const std::string& config_path);
    bool loadDamageTypes(const std::string& config_path);
    
    std::optional<const Combatant*> getPlayerCombatant(const std::string& id) const;
    std::optional<const Combatant*> getEnemyCombatant(const std::string& id) const;
    const std::vector<std::string>& getDamageTypes() const;
};

} // namespace Combatants
```

## Usage Example

```cpp
#include "combatants.hpp"

auto& registry = Combatants::CombatantRegistry::getInstance();

registry.loadDamageTypes("config/damage_types.json");
registry.loadPlayerCombatants("config/player_combatants.json");
registry.loadEnemyCombatants("config/enemy_combatants.json");

auto spearman = registry.getPlayerCombatant("spearman");
if (spearman) {
    auto damage = spearman.value()->getDamage(3);
    std::cout << "Level 3 spearman damage: " 
              << damage.melee << " melee, "
              << damage.ranged << " ranged, "
              << damage.magical << " magical" << std::endl;
}

for (const auto& type : registry.getDamageTypes()) {
    std::cout << "Damage type: " << type << std::endl;
}
```

## Combatant Art (RTS)

Combat renders procedurally by default (class-colored circle + team ring, HP bar,
and facing chevron overlays). Hand-authored art is optional and drops in behind
the same API. The client (`CombatGame.svelte` / `DeployView.svelte`) reads:

```
game/images/combatants/{class}/idle/1.png            # legacy flat, rotated fallback
game/images/combatants/{class}/{clip}/{set}/{n}.png  # directional frames
```

- `{clip}` is an animation clip: `walk` (movement) and `idle` (the stationary
  standing pose). `attack`/`die` are reserved for later.
- `{set}` is an authored view — by convention the compass id `n, ne, e, se, s,
  sw, w, nw`. A view may instead reuse another set with `{ "set": "...",
  "mirror": true }` (horizontal flip across the vertical axis). **Mirroring is
  opt-in per direction**: `"e": "e"` means author your own set with no flip.
- `{n}` runs from `1`, and the frame **count is discovered from the directory
  contents** — an animation may have any number of frames. `idle` typically holds
  a single image (a static standing pose), but a multi-frame `idle` is allowed.

Clips are declared per class in the combatant config (`fps` + optional `views`):

```json
"animations": {
  "walk": {
    "fps": 10,
    "views": {
      "e": "e", "se": "se", "s": "s", "sw": "sw",
      "w": "w", "nw": "nw", "n": "n", "ne": "ne"
    }
  },
  "idle": {
    "fps": 1,
    "views": {
      "e": "e", "se": "se", "s": "s", "sw": "sw",
      "w": "w", "nw": "nw", "n": "n", "ne": "ne"
    }
  }
}
```

`/api/combatGetConfigs` returns these as `combatant_art`, but **only for clips
with an authored set directory on disk**; it discovers the frame count from disk
(`frames`) and narrows `views` to the sets that exist — the client never requests
art that does not exist, and classes without art keep rendering procedurally.
Directional frames are drawn unrotated for the unit's 8-way sector (the facing
chevron snaps to that sector); the legacy flat `idle/1.png` is rotated to the
exact facing and is the fallback when a class has no directional clips.

**Rest pose:** while stationary the client prefers the `idle` clip (a single
image is the standing pose); with no `idle` art it falls back to walk frame 1.

**Filename Convention:**
- Numeric frames: `1.png`, `2.png`, `3.png`, …
- Accepted extensions: `.png`, `.jpg`, `.jpeg`, `.webp`, `.avif`

**Tooling:**
- `check_configs.py` validates the `animations` block (`fps`/`views`) and treats
  combatant art as optional. `frames` is no longer declared — a stale value warns.
- `check_art.py` reports each authored `<class>/<clip>/<set>/` directory that is
  **missing or empty** — never how many frames it contains.

## Default Values

If a field is not specified:

| Field | Default |
|-------|---------|
| Any damage field | 0 |
| Any defense field | 0 |
| Any cost field | 0 |
| `defense` array element | null (no defense at that level) |
| Empty `damage`, `defense`, `movement_speed`, `costs` arrays | 0 at all levels |

## Level 0 Clarification

Level 0 represents a combatant that has not yet been created or is in an invalid state. Combatant stats should always be requested for level >= 1.

## Lenient Parsing

All config files use lenient parsing:

```cpp
nlohmann::json::parse(content, nullptr, true, true);
//                                      ^           ^    ^    ^
//                                      content   cb  allow  ignore
//                                                except comments
```

This allows:
- C-style comments: `// comment` or `/* comment */`
- Trailing commas in arrays and objects
- Relaxed JSON syntax

## PvE Mission Offerings (auto-generated)

PvE missions are **auto-generated, deterministic, per-character offerings**
(`game/config/combat/offerings.json`), not a fixed catalog. A board of
`board.per_tier` offerings exists for each applicable tier (solo / duo /
company), regenerated every hour.

- **Deterministic ids.** `offering_id` encodes
  `(player_id, hour_bucket, tier, index, players, troop_count, power_bucket)`,
  so the offering content is a **pure function of the id** — nothing is stored
  except completions (`character_mission_completions`) and attempts
  (`combat_mission_attempts`). `server/MissionGenerator.{hpp,cpp}` is the pure
  generator; `server/MissionOfferings.{hpp,cpp}` is the DB/validation side.
  The party fields are frozen at generation: a grown party keeps the offer valid,
  a shrunken one voids it.
- **Power.** A shared primitive (`server/RetinueStrength.{hpp,cpp}`) produces a
  single power number for a combatant/retinue/warband — see **Strength** below.
  The party's aggregate `S` is quantized (`power_scale`, geometric by default)
  into the id's `power_bucket`.
- **Tiers.** `solo` (party 1, always), `duo` (party 2, barony only), `company`
  (party 3–5, barony only). A tier's party combines the character with up to
  `max_players - 1` barony members (strongest first):
  `P = Σ sum_power`, `N = Σ unit_count`, `S = P·N^(β−1)`. The actual party size
  and unit count are frozen in the id.
- **Assembly** (`assemble_warband`). The warband is a pure function of
  `(seed, expected_players, party_troop_count, party_power)`:
  1. `ρ` is drawn uniformly from `difficulty.band_min…band_max`; the target is
     `T = ρ·S`.
  2. `e` is drawn from `encounter.eliteness.seed_min…seed_max`, then biased by
     party size (`+players_coeff·(k/players_ref − 1)`) and by retinue size
     (`−troops_coeff·log2(N/troops_ref)`), clamped to `[min, max]`. `e < 0`
     favours cheap classes (horde), `e > 0` expensive ones (elite).
  3. A class is picked with `w(c) ∝ (p_L1(c)/p_ref)^e`, where `p_ref` is the
     geometric mean of the classes' level-1 powers.
  4. The pick's **unmet prerequisites** are resolved recursively (depth-first,
     `encounter.prerequisite_depth`), in dependency order, using the
     `requires: [{unit_class, count}]` lists on `enemy_combatants.json`.
  5. Each unit in the closure is added at a level drawn uniformly in
     `[1, L_fit]`, where `L_fit` is the highest level that keeps the warband's
     aggregate power **strictly under** `T + p_min` (`p_min` = the cheapest
     per-unit power in the monster configs). The first unit that cannot fit ends
     the round (no skipping — an unaffordable boss still leaves its paid escort).
  6. Rounds repeat until a full pass over the classes adds nothing (the **dry
     pass**), capped defensively at 10 000 iterations and `max_units` units.
  7. Entries are merged by `(class, level, group, tactic, aggro_model)`.
- **Level curve.** Combatant level arrays follow
  `m(L) = 1 + 0.1·(L − 1)` (L2 = 1.1, L10 = 1.9): `damage` and `max_hp` each
  scale by `√m` (so the derived power scales by `m`), while `defense`,
  `attack_range`, `attack_interval`, and `movement_speed` stay flat. Level is a
  property of each individual monster, so a warband can mix levels freely.
- **Realized power.** The warband's actual `R = warband_power(entries)` — not
  the target — drives `danger_ratio = R/S`, `danger_band`, and
  `xp_base = max(xp.min, xp.per_power·R)` (× the ruleset's
  `victory_multiplier` on a win).
- **Authenticity** (not eligibility): `mission_offerings::validate_offering_authentic`
  checks the player id, the validity window (`board.validity_hours`, an invite
  grace), and that the id's frozen `players` / `troop_count` / `power_bucket`
  are at or below the character's current party. Once a match is created the
  result is trusted.
- **Rout.** An enemy that breaks (below its class `retreat_threshold`) and stays
  broken flees the field; dead-or-fled = annihilation for the win condition.
- **Counters.** Per-class enemy deaths increment `character_counters`
  (`slain.<class>`), which drive counter-based milestones
  (`progression.json` `milestones`).

### Strength

The strength primitive (`server/RetinueStrength.{hpp,cpp}`) is the one place a
combatant's power is computed; it drives the party aggregate, warband assembly,
the danger estimate, and strength-derived XP — and later matchmaking and the
balance analyzer. It resolves stats through the **same resolver the simulation uses**
(`combat/CombatantStats.hpp` → `combat::resolve_combatant_stats`), so the two can
never drift — including the level-indexed extrapolation rules and the `dark_mage`
level-1 null-defense edge case.

**Per-unit power** (one combatant of class + level):

```
dps      = damage[attack_type] / attack_interval
def_eff  = (0.5·defense.melee + 0.3·defense.ranged + 0.2·defense.magical)
           × direction_defense.front
ehp      = max_hp × (100 + def_eff)/100 / (1 − dodge.front)
power    = dps × ehp × (is_aoe ? aoe_multiplier : 1)
           × (1 + k_range × max(0, attack_range − melee_ref))
```

- The ratio of two units' powers is their **1-vs-1 time-to-kill ratio** (TTK).
- Defense is a weighted average over the three damage types, **front arc only**;
  dodge (also front arc) raises effective HP. **Full health** is assumed — the
  party aggregate always prices a member at 100% (a wounded retinue is
  intentionally over-estimated here; the sim's HP bridge still weakens it in
  battle).
  Equipment is ignored (gear has no combat effect yet — a documented no-op).

**Army strength** (a retinue or a warband — the same function):

```
S = (Σ power) × N^(β−1)      β = 2, N = total unit count
```

- β folds in the **N-vs-1 effect**: β = 1 is linear (duel / frontage-limited),
  β = 2 is square (all units engage). The truth is between; β is the single knob
  to tune against playtests.
- Calibration (sim-verified): one knight's per-unit power is ≈ 8 goblins';
  `army_strength` makes **3 goblins ≈ 1 knight** and **10 goblins beat a knight**.
  A plain `Σ power` reported 10 goblins ≈ 1 knight — the bug this fixes.

The tuning constants (`engagement_exponent`, `defense_weights`, `aoe_multiplier`,
`ranged_bonus_per_unit`, `ranged_melee_ref`) live in the `encounter` block of
`game/config/combat/offerings.json` and are read by
`retinue_strength::load_strength_tuning`; the struct defaults match the shipped
config. Warband **assembly** (see above) consumes the same primitive for every
candidate and prices the finished warband with `warband_power`, so the target,
the danger label, and the simulation all use one scale.