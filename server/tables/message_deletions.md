# message_deletions Table

Admin-only forensic copies of messages deleted during an adult → minor account
demotion. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE message_deletions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    message_id TEXT NOT NULL,
    chat_id TEXT NOT NULL,
    author_character_id INTEGER,
    author_account_id INTEGER NOT NULL,
    custom_body TEXT,
    safe_message_id TEXT,
    gif_id TEXT,
    source_language TEXT,
    reply_to_message_id TEXT,
    reply_custom_excerpt TEXT,
    reply_safe_excerpt TEXT,
    created_at INTEGER NOT NULL,
    deleted_at INTEGER NOT NULL,
    deletion_reason TEXT NOT NULL,
    deleted_by_account_id INTEGER NOT NULL
);
```

## Purpose

When an account is demoted, every message authored by every character on the
account is tombstoned in `messages` and copied here. Normal readers never query
this table. `GET /api/adminGetDeletedMessages` is admin-only and audited.

## Notes

- The original custom body is retained for investigation only.
- Normal message rows get `deleted_at`, `deletion_reason`, and
  `deleted_by_account_id`; their content columns are nulled.
- Translations/reactions for deleted messages are removed.
