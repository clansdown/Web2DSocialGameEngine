# blocks Table

Bidirectional blocks. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE blocks (
    blocker_character_id INTEGER NOT NULL,
    blocked_character_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    change_id INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (blocker_character_id, blocked_character_id)
);
```

## Notes

- A block prevents DMs, @mentions, friend requests, group invites, and
  reactions to the other's messages; messages are hidden both ways.
- Does not delete DM threads.
- Increments `character_relation_counts` for both parties.
