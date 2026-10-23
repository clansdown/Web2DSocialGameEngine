# characters Table

Stores character data and links to user accounts. A user may have multiple characters.

## Schema

```sql
CREATE TABLE characters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    display_name TEXT NOT NULL,
    safe_display_name TEXT NOT NULL,
    level INTEGER DEFAULT 1,
    archetype TEXT,
    sex TEXT,
    avatar_safe_id TEXT,
    avatar_custom_id TEXT,
    avatar_changed_at INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(user_id) REFERENCES users(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique character identifier |
| user_id | INTEGER | NOT NULL FK | Links to users table owner |
| display_name | TEXT | NOT NULL | Character display name (can be customized by adult accounts) |
| safe_display_name | TEXT | NOT NULL | Generated unique safe name (word1+word2) |
| level | INTEGER | DEFAULT 1 | Character level |
| archetype | TEXT | NULLABLE | Starting path: `"wolf_warden"` or `"assarter"` (NULL = not yet chosen) |
| sex | TEXT | NULLABLE | Biological sex: `"male"` or `"female"` (NULL = not yet set) |
| avatar_safe_id | TEXT | NULLABLE | Safe-pool PFP id, gender-qualified (`male/1.png`, `female/1.png`) under `game/images/avatars/safe/{male,female}/`. Must match the character's own `sex` pool. NULL until the picker is answered |
| avatar_custom_id | TEXT | NULLABLE | Custom (18+) PFP filename under `game/uploads/avatars/` (e.g. `12.png`). NULL when none. Never served statically — see `/api/avatar/<id>` |
| avatar_changed_at | INTEGER | NOT NULL DEFAULT 0 | Unix ts of the last PFP change. Drives the per-character 24h cooldown; the first pick is exempt because it starts at 0 |

## Indexes

- Index on `user_id` for character lookups by user

## Relationships

- Many-to-one with `users` via `user_id` foreign key
- One-to-many with `fiefdoms` via `owner_id` foreign key (owner_id references characters.id)

## Notes

- Default level is 1 for new characters
- `safe_display_name` is generated from two safe words (word1 + word2)
- `display_name` can be customized only if the owning user's `adult` flag is true
- `archetype` is set via `/api/setCharacterArchetype` and determines the player's starting mini-game
  - `wolf_warden` → tower defense campaign
  - `assarter` → weeding campaign
- Archetype can only be set once per character (NULL → chosen value, never changed after)
- Accessed by `/api/getCharacter` and `/api/login` endpoints
- **PFP (player representative image)** is variety only, with no gameplay effect.
  The safe pool (`avatar_safe_id`) is public art served from
  `/images/avatars/safe/{male,female}/*` and is **gender-split** like the retinue
  headshots: a character only ever picks from its own `sex` pool, and the server
  validates the pick against the stored `sex` (never the request). The custom
  upload (`avatar_custom_id`) is 18+ only,
  stored outside the static tree, and served solely by the age-gated
  `/api/avatar/<character_id>` route. Resolution: an under-18 viewer always gets
  the owner's safe pick; a custom image is returned only when both the owner and
  the viewer are 18+. Any change is rate-limited to once per 24h per character
  (`avatar_changed_at`). See `docs/retinue_design.md` §8.
- `avatar_*` columns are added by the idempotent migration pattern
  (`ALTER TABLE characters ADD COLUMN ...`) used for `archetype`/`sex`.
- A one-time migration (`migrate_safe_avatar_gender`) prefixes any legacy bare
  `avatar_safe_id` (e.g. `3.png`) with the owning character's sex (`male/` or
  `female/`) after the pool became gender-split. Idempotent — it only touches
  ids without a `/`.
