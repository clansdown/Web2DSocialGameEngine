# chat_invites Table

Pending group invitations. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE chat_invites (
    chat_id TEXT NOT NULL,
    invited_character_id INTEGER NOT NULL,
    invited_by_character_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    change_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (chat_id, invited_character_id)
);
```

## Fields

| Field | Purpose |
|---|---|
| `chat_id` | The group. |
| `invited_character_id` | Invitee. |
| `invited_by_character_id` | Inviter. |
| `expires_at` | `created_at + invite_expiry_seconds`. |
| `change_id` | Global update cursor (delivered via `pollChats.invites`). |

## Notes

- Invites must be accepted (`acceptGroupInvite`); declining or expiry deletes
  the row.
- A blocked inviter cannot invite (checked at invite time).
