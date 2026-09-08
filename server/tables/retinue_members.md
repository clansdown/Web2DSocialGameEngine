# `retinue_members`

The player's army for the realtime combat game. The **knight is the character
itself** (`is_knight = 1`) and is always present; additional members are
recruited from the **recruit market** (`/api/listRecruitCandidates` +
`/api/hireRecruit`) or created by the manor game's `train_troops` action
(currently a stub). Combat matches snapshot active members when a player joins
and write results back after a battle ends.

**No member can permanently die** — the worst combat outcome is infirmary time
(`health` recovering toward 100 over wall-clock time).

## Schema

```sql
CREATE TABLE retinue_members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    display_name TEXT NOT NULL,
    unit_class TEXT NOT NULL,
    is_knight INTEGER NOT NULL DEFAULT 0,
    level INTEGER NOT NULL DEFAULT 1,
    gender TEXT NOT NULL DEFAULT 'male',
    health REAL NOT NULL DEFAULT 100,
    health_updated INTEGER NOT NULL DEFAULT 0,
    priority INTEGER NOT NULL DEFAULT 0,
    weapons TEXT NOT NULL DEFAULT '{}',
    armor TEXT NOT NULL DEFAULT '{}',
    equipment TEXT NOT NULL DEFAULT '{}',
    abilities TEXT NOT NULL DEFAULT '[]',
    status TEXT NOT NULL DEFAULT 'active',
    maintained INTEGER NOT NULL DEFAULT 1,
    created_at INTEGER NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `id` | int | Primary key — doubles as the combat unit id |
| `character_id` | int | Owning character |
| `display_name` | text | Soldier name (knight uses the character's display name) |
| `unit_class` | text | Key into `game/config/player_combatants.json` (`knight`, `spearman`, `archer`, `mage`, ...) |
| `is_knight` | int | 1 = the character itself (auto-created on first access; never hire-able/dismiss-able, free of capacity) |
| `level` | int | Soldier level (drives stats + hire fee from the class config) |
| `gender` | text | `male` \| `female` — drives the recruit name pool and (future) gender-aware prose |
| `health` | real | Persistent health 0..100 (continuous-HP model; no discrete injury statuses) |
| `health_updated` | int | Unix ts of the last health snapshot (recovery is computed from elapsed time) |
| `priority` | int | Strict intra-roster rank — funding order and infirmary-bed order (player editable) |
| `weapons` | text | JSON — equipped weapons (legacy; see `equipment`) |
| `armor` | text | JSON — equipped armor (legacy; see `equipment`) |
| `equipment` | text | JSON — generic slot→item map (`weapon`, `armor`, `mount`, `potions`, ...) |
| `abilities` | text | JSON — learned abilities (deferred until the RTS mechanics land) |
| `status` | text | `active` \| `casualty` \| `retired`; "in the infirmary" is `health` below `retinue.json recovery.min_deploy_hp`, not a status |
| `maintained` | int | Whether the economy tick fully funded this member in the current period (priority order, all-or-nothing; shortfalls auto-imported with fungible money). 1 = funded (default), 0 = unfunded |
| `created_at` | int | Unix timestamp |

## Indexes

- `idx_retinue_members_character` on `character_id`

## Relationships

- `characters.id` → `retinue_members.character_id` (many members per character)

## Retinue Capacity

The number of recruited members is capped by the manor's level via
`retinue.json` `retinue_capacity_by_level[manor_level]` (0 → 24 as the manor
grows to level 10). **The knight does not count** toward the cap. Members are
billeted in the manor house — they consume no arable or forest land.

## Recruit Market

- Offers are **generated, never stored**: deterministic from
  `(character_id, hour bucket)` (`floor(now/3600)`), per-class name pools
  (`game/text/en/names_<class>_male.txt` / `_female.txt`), class gating
  (`requires_manor_level`), and candidate level `1..manor_level + offset`.
- Offer validity is re-derivable; `hireRecruit` accepts an offer from the
  current or previous hour bucket (grace window).
- Hire fees come from the class `costs` array (level-indexed), paid from
  fiefdom stores.

## Usage Notes

- The knight row is created lazily by `retinue_db::get_or_create_knight()`
  (first call to `/api/getRetinue` or any match join) from the character's
  display name, level, and `characters.sex` (gender).
- New hires are inserted via `retinue_db::hire_member()` with full HP and a
  priority below all existing members.
- `status = 'casualty'` is still written by `retinue_db::apply_casualties()`
  (rulesets with `death_handling: permanent` — legacy). The **no-death model**:
  skirmish rulesets use `death_handling: wounded`, and
  `retinue_db::apply_wounds()` drops each fallen member's `health` to their
  **end-of-combat HP%** (per-unit `wound_spec`, recovery clock restarts only if
  the health actually drops; `status` stays active) — they recover, they never die.
- Health regenerates over wall-clock time from `health_updated` via
  `retinue_db::apply_recovery()` (rate = `100/max_recovery_hours × multiplier`,
  where the multiplier is `1` + the fiefdom's completed `class: "infirmary"`
  building `recovery_multiplier` bonuses). Members below
  `retinue.json recovery.min_deploy_hp` are not fieldable ("in the infirmary").
- Combat never reads this table mid-battle — the match holds an in-RAM snapshot
  (`combat_player.retinue`), keeping the realtime path SQLite-free.