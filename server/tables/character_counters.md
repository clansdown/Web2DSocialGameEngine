# character_counters Table

Generic per-character counters, keyed by a namespaced string. The first
consumer is enemy-slain tallies (`slain.<unit_class>`) that drive counter-based
progression milestones ("slain 1 ettin", "slain 100 goblins", …).

## Schema

```sql
CREATE TABLE character_counters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    counter_key TEXT NOT NULL,
    value INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id),
    UNIQUE(character_id, counter_key)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique row identifier |
| character_id | INTEGER | NOT NULL FK | Owning character (characters.id) |
| counter_key | TEXT | NOT NULL | Namespaced counter key (e.g. `slain.ettin`) |
| value | INTEGER | NOT NULL DEFAULT 0 | Running total |
| updated_at | INTEGER | NOT NULL | Unix timestamp of the last increment |

## Indexes

- `idx_character_counters_character` on `(character_id)`.
- `UNIQUE(character_id, counter_key)` — increments use upsert
  (`ON CONFLICT … DO UPDATE SET value = value + excluded.value`).

## Relationships

- Many-to-one with `characters` via `character_id`.

## Notes

- Incremented by `mission_offerings::increment_counters` for **every
  participant** on **every** finished combat attempt (victory or not). Only
  **actual deaths** count — a routed (fled) enemy is not slain.
- Read by `progression::get_counter` and
  `progression::evaluate_milestone_conditions`; `progression.json` `milestones`
  lists `{ id, counter, min }` conditions.
- The namespaced key keeps the table open-ended for future counters beyond
  kills.
- Game.db.
