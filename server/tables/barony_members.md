# barony_members Table

Tracks character membership in baronies. A character can only be in one barony.

## Schema

```sql
CREATE TABLE barony_members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    barony_id INTEGER NOT NULL,
    character_id INTEGER NOT NULL UNIQUE,
    fiefdom_id INTEGER NOT NULL,
    joined_at INTEGER NOT NULL,
    role TEXT NOT NULL DEFAULT 'member',
    FOREIGN KEY(barony_id) REFERENCES baronies(id),
    FOREIGN KEY(character_id) REFERENCES characters(id),
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique membership identifier |
| barony_id | INTEGER | NOT NULL FK | Barony the character belongs to |
| character_id | INTEGER | NOT NULL UNIQUE FK | Character who is a member |
| fiefdom_id | INTEGER | NOT NULL FK | Character's fiefdom within the barony |
| joined_at | INTEGER | NOT NULL | Unix timestamp of joining |
| role | TEXT | NOT NULL DEFAULT 'member' | Role: `member`, `mesne_lord`, or `baron` |

## Indexes

- Index on `barony_id`
- Index on `character_id`

## Relationships

- Many-to-one with `baronies` via `barony_id`
- One-to-one with `characters` via `character_id`
- One-to-one with `fiefdoms` via `fiefdom_id`

## Notes

- `character_id` has a UNIQUE constraint — a character can only be in one barony
- Founder role is `mesne_lord`
- Promotion to `baron` requires 21+ members in the barony with manor_level ≥ 3
