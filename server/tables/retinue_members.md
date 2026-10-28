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
    xp INTEGER NOT NULL DEFAULT 0,
    headshot_id TEXT NOT NULL DEFAULT '',
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
| `xp` | int | Cumulative RTS experience. Earned per-fielded-member, warband-derived, completion-only (rts_design.md §14.1); level-ups happen against the class `max_level` (knight levels independently in the RTS, character level untouched). Added via migration (see below) |
| `headshot_id` | text | Persistent identity: the image path relative to `game/images/retinue/headshots/`, e.g. `male/03.png`. Written once at hire, never changed. The knight keeps `''` and resolves its portrait from `characters.avatar_*` (the owner's PFP). Added via migration (see below) |
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

- Offers are **generated, never stored**: the class/gender/level/headshot
  sequence is deterministic from `(character_id, hour bucket)`
  (`floor(now/3600)`); names come from the account-language pool
  (`game/text/<language>/names_<class>_male.txt` / `_female.txt`, English
  fallback), and class gating (`requires_manor_level`) plus candidate level
  `1..manor_level + offset` apply. Each offer carries `name_language` so hire
  validation uses the pool that produced its name. Hired names are stored as
  generated and do not change when the account language later changes.
- **Identity roll order: class → sex → level → name → headshot.** Names and
  headshots are re-rolled deterministically (walk forward from the drawn index
  in the sorted pool) past anything already used by the character's roster and
  past earlier offers in the same displayed list, so the normal UI never shows a
  duplicate. Headshot pools are scanned + sorted from
  `game/images/retinue/headshots/{male,female}/` (no catalog).
- Offer validity is **structural** at hire: known + unlocked class, in-range
  level, current-or-previous hour bucket, name present in the class/sex pool, and
  an existing headshot for the sex. `hireRecruit` accepts an offer from the
  current or previous hour bucket (grace window). It deliberately does NOT
  regenerate the exact offer or enforce name uniqueness — class/level (and thus
  the fee and stats) are fully validated, so a scripted duplicate is harmless.
- Hire fees come from the class `costs` array (level-indexed), paid from
  fiefdom stores.
- `/api/getRetinue` returns each member's `headshot_id` plus a server-resolved
  `portrait_url` (soldiers → the headshot; the knight → the owner's PFP).

## Usage Notes

- **`xp` column migration:** the `xp` column (default 0) is added to existing
  databases by the same idempotent migration pattern as `player_game_state.honor_name`
  — a `PRAGMA table_info` check followed by `ALTER TABLE ... ADD COLUMN xp
  INTEGER NOT NULL DEFAULT 0` when absent. Reads include the column; combat
  conclusion persists it via `retinue_db::grant_experience` (rts_design.md §14).

- **`headshot_id` column migration:** added by the same idempotent pattern
  (`ALTER TABLE retinue_members ADD COLUMN headshot_id TEXT NOT NULL DEFAULT ''`).
  No backfill is needed (no pre-existing recruited members at the time of
  introduction); the knight's empty value is the intended steady state.

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