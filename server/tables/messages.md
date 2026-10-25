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
    safe_message_id TEXT,
    source_language TEXT,
    source_language_confidence REAL,
    gif TEXT,
    gif_id TEXT,
    reply_to_message_id TEXT,
    reply_author_character_id INTEGER,
    reply_excerpt TEXT,
    reply_safe_excerpt TEXT,
    reply_source_language TEXT,
    system_id TEXT,
    system_args TEXT,
    created_at INTEGER NOT NULL,
    edited_at INTEGER,
    deleted_at INTEGER,
    deletion_reason TEXT,
    deleted_by_account_id INTEGER,
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
| `body` | Adult custom text. NULL/empty for a safe message. Never sent to a minor. |
| `safe_message_id` | Server-owned safe catalog id (`safe_messages.json`). A minor may only see/author messages with this set. |
| `source_language` / `_confidence` | Detected custom-text source language (baseline engine). Empty for safe messages. |
| `gif` | Legacy arbitrary GIF JSON. **Never read**; use `gif_id`. |
| `gif_id` | Server-owned GIF catalog id (`safe_gifs.json`). Minor visibility requires `minor_safe`. |
| `reply_excerpt` | Adult custom parent excerpt snapshot. |
| `reply_safe_excerpt` | Safe parent excerpt snapshot; minors receive only this. |
| `reply_*` | Snapshot of the replied-to message (excerpt + author + language). |
| `system_id` / `system_args` | System event (`member_joined`, `member_left`, `owner_transferred`, `member_kicked`). |
| `edited_at` | Last edit time; versions in `message_versions`. |
| `deleted_at` | Tombstone marker (body set NULL). |
| `version` | Edit version counter. |
| `change_id` | Global update cursor (also bumped by reaction changes). |

## Notes

- Messages are kept forever; only library translations are age-reapable.
- Edits invalidate stored translations for the message.
