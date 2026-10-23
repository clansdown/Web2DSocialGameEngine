# friend_requests Table

Pending friend requests (one-directional). Stored in `chat.db`.

## Schema

```sql
CREATE TABLE friend_requests (
    from_character_id INTEGER NOT NULL,
    to_character_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    change_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (from_character_id, to_character_id)
);
```

## Notes

- Capped at `max_friend_requests_per_day` per sender.
- Blocked either way prevents sending/accepting.
- Accepting inserts two `friends` rows and cancels both directions.
