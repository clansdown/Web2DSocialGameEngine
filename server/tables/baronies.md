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
    FOREIGN KEY(owner_character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique barony identifier |
| name | TEXT | NOT NULL UNIQUE | Barony display name |
| owner_character_id | INTEGER | NOT NULL FK | Founder/owner character |
| description | TEXT | DEFAULT '' | Optional description |
| created_at | INTEGER | NOT NULL | Unix timestamp of creation |

## Indexes

- Index on `owner_character_id`

## Relationships

- Many-to-one with `characters` via `owner_character_id`
- One-to-many with `barony_members` via `id`

## Notes

- Created via `/api/createBarony`
- Listed via `/api/getBaronies`
- Founder role is `mesne_lord`, becomes `baron` when conditions are met (21+ members with manor ≥ 3)
