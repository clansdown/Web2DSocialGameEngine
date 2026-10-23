# baronies Table

Stores barony (alliance) data. Each barony has a founder and members.

## Schema

```sql
CREATE TABLE baronies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    owner_character_id INTEGER NOT NULL,
    description TEXT DEFAULT '',
    created_at INTEGER NOT NULL,
    baron_character_id INTEGER,
    defunct_at INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(owner_character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique barony identifier |
| name | TEXT | NOT NULL UNIQUE | Barony display name |
| owner_character_id | INTEGER | NOT NULL FK | Founder/owner character (reassigned to a successor when the founder leaves a barony that still has members) |
| description | TEXT | DEFAULT '' | Optional description |
| created_at | INTEGER | NOT NULL | Unix timestamp of creation |
| baron_character_id | INTEGER | NULLABLE | Current baron (reassignable if leadership changes) |
| defunct_at | INTEGER | NOT NULL DEFAULT 0 | Unix timestamp the barony was tombstoned; `0` = active. Added by `migrate_barony_defunct` |

## Indexes

- Index on `owner_character_id`

## Relationships

- Many-to-one with `characters` via `owner_character_id`
- Many-to-one with `characters` via `baron_character_id`
- One-to-many with `barony_members` via `id`

## Notes

- Created via `/api/createBarony` (baron-track founder) or `/api/applyForMesneLord`
  (a qualified freeholder founds an Honor under its pre-elevation name).
- Listed via `/api/getBaronies`, which **excludes defunct** baronies
  (`defunct_at > 0`).
- Founder role is `mesne_lord`, becomes `baron` when conditions are met (21+ members with manor ≥ 3)
- `owner_character_id` is the founder; `baron_character_id` tracks the current baron and is
  set by the promotion check. A baron can be replaced by reassigning `baron_character_id` (along with
  the matching `barony_members.role` change).
- **Founder succession:** `/api/leaveBarony` reassigns both `owner_character_id`
  and `baron_character_id` to a named successor when the founder leaves a barony
  that still has other members (the successor's role becomes `baron`).
- **Tombstoning:** when the founder leaves as the **last** member the barony is
  not deleted — `defunct_at` is set, it is hidden from `/api/getBaronies`, its
  chat is archived, and the name stays permanently reserved (case-insensitive).
- Existing baronies created before this field existed keep `baron_character_id = NULL` until their
  promotion check runs.
