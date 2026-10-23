# character_letters Table

Stores in-game letters (the king's missives and future notices) sent to a
character. A letter is a **text-system key** plus timestamps — the client
renders it through the character-text path so gender / name / honor substitution
still applies. See `api/getLetters.md` and `api/markLetterRead.md`.

## Schema

```sql
CREATE TABLE character_letters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    letter_key TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    read_at INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique letter identifier |
| character_id | INTEGER | NOT NULL FK | Recipient character (characters.id) |
| letter_key | TEXT | NOT NULL | Text-system key (e.g. `letter_mesne_lord_eligible`) resolved via `game/text/<lang>/<letter_key>.txt` |
| created_at | INTEGER | NOT NULL | Unix timestamp the letter was sent |
| read_at | INTEGER | NOT NULL DEFAULT 0 | Unix timestamp marked read; `0` = unread |

## Indexes

- `idx_character_letters_character` on `(character_id, created_at)` — drives
  the per-character newest-first listing and unread lookup.

## Relationships

- Many-to-one with `characters` via `character_id`.
- No join to the text system: `letter_key` is matched to `game/text/*` at read
  time.

## Notes

- Written by `letters::send_letter` (`server/Letters.cpp`); the mesne-lord
  eligibility letter (`letter_mesne_lord_eligible`) is sent exactly once by
  `progression::check_freehold_qualification`.
- `letters::get_letters` returns rows newest-first; `oldest_unread_letter`
  returns the single oldest row with `read_at = 0` (drives the
  `/api/getPlayerState` overlay).
- `/api/markLetterRead` sets `read_at` (idempotent); `/api/getLetters` lists.
- Game.db. The client never guesses letter keys — it fetches the id + key and
  resolves the text through the character-text endpoint.
