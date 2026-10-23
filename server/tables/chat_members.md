# chat_members Table

Per-character membership and per-chat settings. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE chat_members (
    chat_id TEXT NOT NULL,
    character_id INTEGER NOT NULL,
    role TEXT NOT NULL DEFAULT 'member',
    joined_at INTEGER NOT NULL,
    left_at INTEGER,
    sticky INTEGER NOT NULL DEFAULT 0,
    muted INTEGER NOT NULL DEFAULT 0,
    notify_level TEXT NOT NULL DEFAULT 'all',
    last_read_seq INTEGER NOT NULL DEFAULT 0,
    unread_count INTEGER NOT NULL DEFAULT 0,
    change_id INTEGER NOT NULL,
    PRIMARY KEY (chat_id, character_id)
);
```

## Fields

| Field | Purpose |
|---|---|
| `role` | `owner` / `moderator` / `member`. |
| `left_at` | NULL while active; set on leave/kick/ban (history retained). |
| `sticky` | Pinned to the top of the chat list. |
| `muted` | Chat-mute: suppresses all notifications, including mentions. |
| `notify_level` | `all` / `mentions` / `none` (push eligibility). |
| `last_read_seq` | Read cursor for the chat. |
| `unread_count` | Count of eligible unread messages. |
| `change_id` | Global update cursor. |

## Notes

- Wildlands is never materialized; rows appear only if a character sets a
  setting or read state there.
- Rejoining a group upserts `left_at = NULL` (history retained).
- Unread increments use the `character_relation_counts` fast path.
