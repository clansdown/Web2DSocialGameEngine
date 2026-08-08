# `retinue_members`

The player's army for the realtime combat game. The **knight is the
character itself** (`is_knight = 1`); additional soldiers are created by the
manor game's `train_troops` action (currently a stub). Combat matches snapshot
the active members when a player joins, and write casualties back after a
battle ends.

## Schema

```sql
CREATE TABLE retinue_members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    display_name TEXT NOT NULL,
    unit_class TEXT NOT NULL,
    is_knight INTEGER NOT NULL DEFAULT 0,
    level INTEGER NOT NULL DEFAULT 1,
    weapons TEXT NOT NULL DEFAULT '{}',
    armor TEXT NOT NULL DEFAULT '{}',
    abilities TEXT NOT NULL DEFAULT '[]',
    status TEXT NOT NULL DEFAULT 'active',
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
| `unit_class` | text | Key into `game/config/player_combatants.json` (`knight`, `spearman`, ...) or `heroes.json` |
| `is_knight` | int | 1 = the character itself (auto-created on first access) |
| `level` | int | Soldier level (drives stats from the combatant config) |
| `weapons` | text | JSON — equipped weapons (empty until manor equipment exists) |
| `armor` | text | JSON — equipped armor |
| `abilities` | text | JSON — learned abilities |
| `status` | text | `active` \| `casualty` \| `retired` |
| `created_at` | int | Unix timestamp |

## Indexes

- `idx_retinue_members_character` on `character_id`

## Relationships

- `characters.id` → `retinue_members.character_id` (many members per character)

## Usage Notes

- The knight row is created lazily by `retinue_db::get_or_create_knight()`
  (first call to `/api/getRetinue` or any match join) from the character's
  display name and level.
- `status = 'casualty'` is written by `retinue_db::apply_casualties()` after
  a combat match with permanent-death rules ends; casualties are excluded from
  future matches until revived by future manor-game mechanics.
- Combat never reads this table mid-battle — the match holds an in-RAM
  snapshot (`combat_player.retinue`), keeping the realtime path SQLite-free.
