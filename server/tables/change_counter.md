# change_counter Table

Single-row global cursor allocator for `chat.db`. Every mutated chat row is
stamped with a fresh `change_id`, so a poll cursor (one integer) can detect
updates across all tables. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE change_counter (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    next_change_id INTEGER NOT NULL
);
```

Seeded once with `(1, 1)` on schema creation.

## Fields

| Field | Type | Purpose |
|---|---|---|
| `id` | INTEGER | Always 1. |
| `next_change_id` | INTEGER | The next value to allocate. `allocate_change_id()` returns it and increments. |

## Notes

- `current_change_id()` returns `next_change_id - 1` (the max allocated).
- Access is serialized by the single-threaded HTTP loop.
