# fiefdoms Table

Stores player territory holdings and their locations.

## Schema

```sql
CREATE TABLE fiefdoms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    x INTEGER NOT NULL,
    y INTEGER NOT NULL,
    FOREIGN KEY(owner_id) REFERENCES players(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique fiefdom identifier |
| owner_id | INTEGER | NOT NULL FK | Owning player (players.id) |
| name | TEXT | NOT NULL | Fiefdom name |
| x | INTEGER | NOT NULL | X coordinate in world |
| y | INTEGER | NOT NULL | Y coordinate in world |

## Indexes

- Index on `owner_id` for player fiefdom lookups

## Relationships

- Many-to-one with `players` via `owner_id` foreign key

## Notes

- x and y coordinates represent grid world position
- Accessed by `/api/getFiefdom` endpoint (stub)