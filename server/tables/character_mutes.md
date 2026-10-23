# character_mutes Table

Per-character "person mute" (one-directional). Stored in `chat.db`.

## Schema

```sql
CREATE TABLE character_mutes (
    character_id INTEGER NOT NULL,
    muted_character_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    change_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (character_id, muted_character_id)
);
```

## Notes

- No notifications from the muted character; their group messages are hidden by
  default (collapsible).
- Increments `character_relation_counts` for both parties.
- Distinct from a **chat-mute** (`chat_members.muted`).
