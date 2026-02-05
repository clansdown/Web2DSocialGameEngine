# message_queues Table

Tracks unread message counts per character.

## Schema

```sql
CREATE TABLE message_queues (
    character_id INTEGER PRIMARY KEY NOT NULL,
    unread_count INTEGER DEFAULT 0
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| character_id | INTEGER | PRIMARY KEY NOT NULL | Character identifier |
| unread_count | INTEGER | DEFAULT 0 | Number of unread messages |

## Indexes

- Primary key on `character_id`

## Relationships

- Links to `characters` table via `character_id`

## Notes

- Maintained by triggers when new messages are added to `player_messages`
- Stored in `messages.db` database (separate from game.db)
- Atomic increment/decrement operations required