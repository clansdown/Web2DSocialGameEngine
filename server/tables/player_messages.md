# player_messages Table

Stores direct messages between players.

## Schema

```sql
CREATE TABLE player_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    from_player_id INTEGER NOT NULL,
    to_player_id INTEGER NOT NULL,
    message TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    read INTEGER DEFAULT 0
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique message identifier |
| from_player_id | INTEGER | NOT NULL | Sender player ID |
| to_player_id | INTEGER | NOT NULL | Recipient player ID |
| message | TEXT | NOT NULL | Message content |
| timestamp | INTEGER | NOT NULL | Unix timestamp of message |
| read | INTEGER | DEFAULT 0 | Read status (0=unread, 1=read) |

## Indexes

- Index on `to_player_id` for recipient message lookups
- Index on `from_player_id` for sender message history
- Index on `timestamp` for chronological queries

## Relationships

- Links to `players` table (from_player_id and to_player_id reference players.id)

## Notes

- Stored in `messages.db` database (separate from game.db)
- Triggers updates to `message_queues` unread counts