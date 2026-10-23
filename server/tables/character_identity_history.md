# character_identity_history Table

Prior display names/avatars, retained so an admin can force a revert (chat
identity lock). Stored in **`game.db`** (not `chat.db`).

## Schema

```sql
CREATE TABLE character_identity_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    kind TEXT NOT NULL,
    value TEXT NOT NULL,
    changed_at INTEGER NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Purpose |
|---|---|
| `kind` | `name` or `avatar`. |
| `value` | The prior value. Avatar values are safe-pool ids, or `custom`. |
| `changed_at` | When the value was replaced. |

## Notes

- Rows are appended whenever a player changes their name/avatar.
- See `server/docs/chat_identity_lock.md` and the `adminGetIdentityHistory` /
  `adminLockIdentity` endpoints.
