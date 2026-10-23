# push_preferences Table

Per-character push preferences. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE push_preferences (
    character_id INTEGER PRIMARY KEY,
    push_enabled INTEGER NOT NULL DEFAULT 1,
    dm_from_friends INTEGER NOT NULL DEFAULT 1,
    group_mentions INTEGER NOT NULL DEFAULT 0
);
```

## Notes

- Defaults: push on, DMs from friends on, group mentions **off**.
- Absence of a row is treated as the defaults.
- `push_enabled = 0` suppresses all push for that character.
