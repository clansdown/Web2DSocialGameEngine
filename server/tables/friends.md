# friends Table

The friend graph (stored symmetrically). Stored in `chat.db`.

## Schema

```sql
CREATE TABLE friends (
    character_id INTEGER NOT NULL,
    friend_character_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    change_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (character_id, friend_character_id)
);
```

## Notes

- Accepting a request inserts both directions.
- `change_id` drives `pollChats.friend_events`.
- Friendship enables eager DM creation, larger DM limits, and default DM push.
