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
