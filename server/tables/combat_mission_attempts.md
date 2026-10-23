# combat_mission_attempts Table

A log of every finished PvE combat mission attempt (win, loss, draw, or
forfeit). It records who took part and the warband's end state, which is the
seam for a future "the warband starts with only what was left" retry option
(not implemented for v1).

## Schema

```sql
CREATE TABLE combat_mission_attempts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    offering_id TEXT NOT NULL,
    host_character_id INTEGER NOT NULL,
    match_id TEXT NOT NULL,
    started_at INTEGER NOT NULL,
    ended_at INTEGER,
    result TEXT,
    participants_json TEXT NOT NULL DEFAULT '[]',
    warband_end_state_json TEXT NOT NULL DEFAULT '[]',
    FOREIGN KEY(host_character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique row identifier (the `mission_attempt_id` held by the match) |
| offering_id | TEXT | NOT NULL | The offering played |
| host_character_id | INTEGER | NOT NULL FK | The host (characters.id) |
| match_id | TEXT | NOT NULL | The RAM match id (`m<n>`) |
| started_at | INTEGER | NOT NULL | Unix timestamp the match was created |
| ended_at | INTEGER | nullable | Unix timestamp the battle ended |
| result | TEXT | nullable | `victory` / `defeat` / `draw` |
| participants_json | TEXT | NOT NULL | `[{character_id, fielded_member_ids}]` |
| warband_end_state_json | TEXT | NOT NULL | `{deaths:{class:count}, fled:{class:count}, survivors:[{unit_class,hp}]}` |

## Indexes

- `idx_combat_attempts_offering` on `(offering_id)`.

## Relationships

- Many-to-one with `characters` via `host_character_id`.

## Notes

- Created by `mission_offerings::record_attempt_start` at `combatCreate`
  (the decision to start is trusted, so the result is honoured regardless of
  later power changes) and finalized by `mission_offerings::record_attempt_end`
  in `combat_match_manager::on_match_end`.
- `warband_end_state_json` is derived from the match's per-class death/rout
  tallies plus the surviving enemies' HP — enough to resume a retry later.
- Game.db.
