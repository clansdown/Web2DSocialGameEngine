# dukedoms Table

Stores dukedom (alliance) data. Each dukedom has a founder and members.

## Schema

```sql
CREATE TABLE dukedoms (
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
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique dukedom identifier |
| name | TEXT | NOT NULL UNIQUE | Dukedom display name |
| owner_character_id | INTEGER | NOT NULL FK | Founder/owner character |
| description | TEXT | DEFAULT '' | Optional description |
| created_at | INTEGER | NOT NULL | Unix timestamp of creation |

## Indexes

- Index on `owner_character_id`

## Relationships

- Many-to-one with `characters` via `owner_character_id`
- One-to-many with `dukedom_members` via `id`

## Notes

- Created via `/api/createDukedom`
- Listed via `/api/getDukedoms`
- Founder role is `mesne_lord`, becomes `duke` when conditions are met (21+ members with manor ≥ 3)
