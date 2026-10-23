# bookmarks Table

Private per-character message bookmarks. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE bookmarks (
    character_id INTEGER NOT NULL,
    message_id TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (character_id, message_id)
);
```

## Notes

- Private: never delivered to other members.
- Listed by `getChatBookmarks`.
