# messages Table

A chat message (or system event). Stored in `chat.db`.

## Schema

```sql
CREATE TABLE messages (
    id TEXT PRIMARY KEY,
    chat_id TEXT NOT NULL,
    seq INTEGER NOT NULL,
    author_character_id INTEGER,
    body TEXT,
    source_language TEXT,
    source_language_confidence REAL,
    gif TEXT,
    reply_to_message_id TEXT,
    reply_author_character_id INTEGER,
    reply_excerpt TEXT,
    reply_source_language TEXT,
    system_id TEXT,
    system_args TEXT,
    created_at INTEGER NOT NULL,
    edited_at INTEGER,
    deleted_at INTEGER,
    version INTEGER NOT NULL DEFAULT 1,
    change_id INTEGER NOT NULL,
    UNIQUE (chat_id, seq)
);
```

## Fields

| Field | Purpose |
|---|---|
| `seq` | Per-chat monotonic position. |
| `author_character_id` | NULL for system messages. |
| `source_language` / `_confidence` | Detected source language (baseline engine). |
| `gif` | JSON of one server-provided GIF (`{provider,id,url,preview_url}`), else NULL. |
| `reply_*` | Snapshot of the replied-to message (excerpt + author + language). |
| `system_id` / `system_args` | System event (`member_joined`, `member_left`, `owner_transferred`, `member_kicked`). |
| `edited_at` | Last edit time; versions in `message_versions`. |
| `deleted_at` | Tombstone marker (body set NULL). |
| `version` | Edit version counter. |
| `change_id` | Global update cursor (also bumped by reaction changes). |

## Notes

- Messages are kept forever; only library translations are age-reapable.
- Edits invalidate stored translations for the message.
