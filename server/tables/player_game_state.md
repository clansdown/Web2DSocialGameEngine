# player_game_state Table

Stores the current progression state of each character: their game phase, the
mini-game/level currently being played, and the aspirational barony (honor) name
chosen when the player opts into the baron track.

## Schema

```sql
CREATE TABLE player_game_state (
    character_id INTEGER PRIMARY KEY NOT NULL,
    game_phase TEXT NOT NULL DEFAULT 'initial_mission',
    current_mini_game TEXT,
    current_level_id INTEGER,
    base_unlocked INTEGER NOT NULL DEFAULT 0,
    entered_at INTEGER NOT NULL DEFAULT 0,
    last_updated INTEGER NOT NULL DEFAULT 0,
    honor_name TEXT,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

> `land_patent_acknowledged` is added by migration (see below) and `honor_name`
> by `migrate_honor_name` — neither appears in the base `CREATE TABLE`.

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| character_id | INTEGER | PRIMARY KEY NOT NULL FK | Owning character (characters.id) |
| game_phase | TEXT | NOT NULL DEFAULT 'initial_mission' | Progression phase (see below) |
| current_mini_game | TEXT | nullable | Mini-game currently being played (e.g. "tower_defense") |
| current_level_id | INTEGER | nullable | Level being played within the current mini-game |
| base_unlocked | INTEGER | NOT NULL DEFAULT 0 | 1 once the player has founded their manor (sandbox) |
| entered_at | INTEGER | NOT NULL DEFAULT 0 | Timestamp when the current phase/level was entered |
| last_updated | INTEGER | NOT NULL DEFAULT 0 | Timestamp of last state change |
| honor_name | TEXT | nullable | The aspiring barony's honor name chosen at baron-track start |

## Game Phases

| Phase | Meaning |
|-------|---------|
| `initial_mission` | Completing the 9 archetype intro levels |
| `land_patent` | Land patent earned; choose join a barony or start your own |
| `baron_track` | Grinding baron levels 10–25 toward `baron_right` |
| `baron_right` | Baron track complete; ready to found the barony |
| `sandbox` | Manor founded; open-ended play |

## Indexes

- Primary key `character_id` (one row per character)

## Relationships

- One-to-one with `characters` via `character_id` primary key
- One-to-many with `mini_game_progress` (level completion, fetched alongside)

## Notes

- `honor_name` is captured by `/api/startBaronTrack` (required, ≤ 64 chars,
  unique case-insensitively against `baronies.name`) and reused as the prefill
  for `/api/createBarony`. It is substituted for the `{honor_name}` token in
  `barony_patent_earned.txt` by `/api/getCharacterTexts`.
- `land_patent_acknowledged` (INTEGER NOT NULL DEFAULT 0) is added by
  `migrate_land_patent_acknowledged`; it tracks whether the player has dismissed
  the land-patent overlay.
- The `{honor_name}` substitution only applies when the value is non-empty;
  otherwise the token is left as written.
- Accessed by `/api/getPlayerState`.
