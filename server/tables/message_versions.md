# message_versions Table

Edit history for messages. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE message_versions (
    message_id TEXT NOT NULL,
    version INTEGER NOT NULL,
    body TEXT,
    editor_character_id INTEGER,
    edited_at INTEGER,
    PRIMARY KEY (message_id, version)
);
```

## Fields

| Field | Purpose |
|---|---|
| `message_id` | The edited message. |
| `version` | New version number (the message's `version`). |
| `body` | The edited body. |
| `editor_character_id` | Author who edited. |
| `edited_at` | Edit time. |

## Notes

- Only the author may edit; the current body lives on `messages`.
