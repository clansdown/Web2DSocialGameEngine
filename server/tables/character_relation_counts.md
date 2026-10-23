# character_relation_counts Table

Fast-path counter: the number of block/mute relationships a character
participates in, in either direction. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE character_relation_counts (
    character_id INTEGER PRIMARY KEY,
    relation_count INTEGER NOT NULL DEFAULT 0
);
```

## Notes

- When a message author's count is **0** (the common case), unread increments
  and poll filtering skip all per-member block/mute checks and use a single bulk
  `UPDATE`.
- Incremented/decremented on block/unblock and mute/unmute for both parties.
- Absence of a row means 0.
