# players Table

Stores player character data and links to user accounts.

## Schema

```sql
CREATE TABLE players (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    level INTEGER DEFAULT 1,
    FOREIGN KEY(user_id) REFERENCES users(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique player identifier |
| user_id | INTEGER | NOT NULL FK | Links to users table owner |
| name | TEXT | NOT NULL | Player character name |
| level | INTEGER | DEFAULT 1 | Player character level |

## Indexes

- Index on `user_id` for player lookups

## Relationships

- Many-to-one with `users` via `user_id` foreign key
- One-to-many with `fiefdoms` via `owner_id` foreign key (owner_id references players.id)

## Notes

- Default level is 1 for new players
- Accessed by `/api/getPlayer` endpoint