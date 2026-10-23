# chat_bans Table

Chat-level bans applied by owners/moderators. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE chat_bans (
    chat_id TEXT NOT NULL,
    character_id INTEGER NOT NULL,
    banned_by_character_id INTEGER,
    reason TEXT,
    created_at INTEGER NOT NULL,
    change_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (chat_id, character_id)
);
```

## Notes

- Banning sets the member's `left_at` and records a `moderation_log` entry.
- A banned character cannot be invited or accept an invite until unbanned.
- The owner cannot be banned; only owner/moderator may ban.
