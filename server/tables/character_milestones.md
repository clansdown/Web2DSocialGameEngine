# character_milestones Table

Stores generic per-character progression milestones — achievements that are not
tied to the mini-game track. The first consumer is the freehold → mesne-lord
qualification: a milestone (e.g. `ettin_slain`) is one of the requirements in
`game/config/progression.json`.

## Schema

```sql
CREATE TABLE character_milestones (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    milestone_id TEXT NOT NULL,
    achieved_at INTEGER NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id),
    UNIQUE(character_id, milestone_id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique row identifier |
| character_id | INTEGER | NOT NULL FK | Owning character (characters.id) |
| milestone_id | TEXT | NOT NULL | Milestone key (e.g. `ettin_slain`) |
| achieved_at | INTEGER | NOT NULL | Unix timestamp the milestone was first earned |

## Indexes

- `idx_character_milestones_character` on `(character_id)`.
- `UNIQUE(character_id, milestone_id)` prevents duplicate grants (grants are
  insert-or-ignore).

## Relationships

- Many-to-one with `characters` via `character_id`.

## Notes

- Granted by `progression::grant_milestone` (`server/Progression.cpp`), which
  uses `INSERT OR IGNORE` so re-earning a milestone never errors and never
  overwrites `achieved_at`.
- RTS milestones are counter-based: `game/config/progression.json` `milestones`
  lists conditions (`{ "id": "ettin_slain", "counter": "slain.ettin", "min": 1 }`).
  Enemy deaths are tallied per class into `character_counters` after every
  combat attempt; `progression::evaluate_milestone_conditions` grants each
  satisfied `id` in `combat_match_manager::on_match_end`. The Ettin milestone is
  granted when an Ettin is **slain** (any generated high-tier offering may field
  one), regardless of the overall match outcome.
- Read by `progression::has_milestone` /
  `meets_mesne_lord_requirements`; the freehold qualification gate checks every
  milestone listed in `mesne_lord.requirements.milestones`.
- Game.db.
