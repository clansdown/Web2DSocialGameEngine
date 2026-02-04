# message_queues Table

Tracks unread message counts per player.

## Schema

```sql
CREATE TABLE message_queues (
    player_id INTEGER PRIMARY KEY NOT NULL,
    unread_count INTEGER DEFAULT 0
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| player_id | INTEGER | PRIMARY KEY NOT NULL | Player identifier |
| unread_count | INTEGER | DEFAULT 0 | Number of unread messages |

## Indexes

- Primary key on `player_id`

## Relationships

- Links to `players` table via `player_id`

## Notes

- Maintained by triggers when new messages are added to `player_messages`
- Stored in `messages.db` database (separate from game.db)
- Atomic increment/decrement operations required