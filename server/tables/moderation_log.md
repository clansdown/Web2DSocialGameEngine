# moderation_log Table

Append-only moderation/admin audit log, retained forever. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE moderation_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at INTEGER NOT NULL,
    actor_character_id INTEGER,
    action TEXT NOT NULL,
    chat_id TEXT,
    target_character_id INTEGER,
    payload TEXT
);
```

## Field notes

| Field | Purpose |
|---|---|
| `action` | `kick`, `ban`, `unban`, `identity_lock`, ... |
| `payload` | JSON with action-specific details (e.g. reason, lock flags). |

## Notes

- Written by `log_moderation`; read (admins only) by `getModerationLog`.
