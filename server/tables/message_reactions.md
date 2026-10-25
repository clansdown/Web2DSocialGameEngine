# message_reactions Table

Emoji reactions. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE message_reactions (
    message_id TEXT NOT NULL,
    character_id INTEGER NOT NULL,
    emoji TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (message_id, character_id, emoji)
);
```

## Fields

| Field | Purpose |
|---|---|
| `message_id` | The reacted message. |
| `character_id` | Reacting character. |
| `emoji` | **Reaction catalog id** (`safe_reactions.json`, e.g. `thumbs_up`). The legacy column name is retained; arbitrary emoji are rejected. Minor authoring/visibility requires `minor_safe`. |

## Notes

- Adding/removing a reaction bumps the **parent message's** `change_id`, so
  reaction changes ride the normal message delta (no separate cursor).
- A blocked character cannot react to a blocked user's message.
