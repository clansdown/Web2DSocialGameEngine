# dukedom_members Table

Tracks character membership in dukedoms. A character can only be in one dukedom.

## Schema

```sql
CREATE TABLE dukedom_members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    dukedom_id INTEGER NOT NULL,
    character_id INTEGER NOT NULL UNIQUE,
    fiefdom_id INTEGER NOT NULL,
    joined_at INTEGER NOT NULL,
    role TEXT NOT NULL DEFAULT 'member',
    FOREIGN KEY(dukedom_id) REFERENCES dukedoms(id),
    FOREIGN KEY(character_id) REFERENCES characters(id),
    FOREIGN KEY(fiefdom_id) REFERENCES fiefdoms(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique membership identifier |
| dukedom_id | INTEGER | NOT NULL FK | Dukedom the character belongs to |
| character_id | INTEGER | NOT NULL UNIQUE FK | Character who is a member |
| fiefdom_id | INTEGER | NOT NULL FK | Character's fiefdom within the dukedom |
| joined_at | INTEGER | NOT NULL | Unix timestamp of joining |
| role | TEXT | NOT NULL DEFAULT 'member' | Role: `member`, `mesne_lord`, or `duke` |

## Indexes

- Index on `dukedom_id`
- Index on `character_id`

## Relationships

- Many-to-one with `dukedoms` via `dukedom_id`
- One-to-one with `characters` via `character_id`
- One-to-one with `fiefdoms` via `fiefdom_id`

## Notes

- `character_id` has a UNIQUE constraint — a character can only be in one dukedom
- Founder role is `mesne_lord`
- Promotion to `duke` requires 21+ members in the dukedom with manor_level ≥ 3
