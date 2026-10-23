# character_mission_completions Table

Records which **one-shot PvE mission offerings** a character has completed.
Offerings are auto-generated and deterministic (see `getCombatOfferings`); they
are never stored, so this table is the only per-offering state — a completed
offering is dropped from the character's board.

## Schema

```sql
CREATE TABLE character_mission_completions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    offering_id TEXT NOT NULL,
    completed_at INTEGER NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id),
    UNIQUE(character_id, offering_id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique row identifier |
| character_id | INTEGER | NOT NULL FK | The participant (characters.id) |
| offering_id | TEXT | NOT NULL | The offering id (`<player_id>.<hour>.<tier>.<index>.<players>.<troop_count>.<power_bucket>`) |
| completed_at | INTEGER | NOT NULL | Unix timestamp the mission was won |

## Indexes

- `idx_mission_completions_character` on `(character_id)`.
- `UNIQUE(character_id, offering_id)` — completions are insert-or-ignore.

## Relationships

- Many-to-one with `characters` via `character_id`.

## Notes

- Written by `mission_offerings::record_completion` on a **won** match, for
  **every participant** (so a joiner can't re-farm the same offering). A
  defeat/draw/forfeit does not consume the offering — it can be retried.
- Read by `mission_offerings::offering_is_completed` (board filtering and the
  `combatCreate` re-check).
- The `offering_id` embeds the **host's** `player_id`, so two characters'
  boards never collide; join-by-code is the co-op sharing path.
- Game.db.
