# `character_monthly_pass`

The **current** per-character monthly-pass entitlement. A row exists only for a
character that has received a pass; absence means “no pass”. The row is mutable
and always reflects the latest state. Every transition is also appended to
`character_monthly_pass_log` (`server/tables/character_monthly_pass_log.md`).

## Schema

```sql
CREATE TABLE character_monthly_pass (
    character_id INTEGER PRIMARY KEY,
    expires_at INTEGER NOT NULL DEFAULT 0,
    permanent INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `character_id` | int | Owning character (primary key — one row per character) |
| `expires_at` | int | Unix expiry timestamp; `0` when permanent or absent |
| `permanent` | int | `1` = never expires; takes precedence over `expires_at` |
| `updated_at` | int | Unix timestamp of the last state change |

## Relationships

- One row per `characters.id`.
- `character_id` → `characters.id`.
- History: `character_monthly_pass_log.character_id`.

## Usage Notes

- **Staff-only for now.** The staff console can grant an arbitrary number of
  31-day months, mark a character permanent, or revoke the pass. Purchases and
  actual monthly-pass content are deferred (`rts_design.md` §18).
- `active` is derived: `permanent = 1 OR expires_at > now`.
- Granting months stacks on `max(now, expires_at)`.
- Granting months to an already-permanent character is rejected as
  `already_permanent`; revoking an inactive pass is rejected as
  `pass_not_active`.
- Revoking deletes the row. The append-only log keeps the full history.
- Written by `server/MonthlyPass.cpp`; read by the staff account view.
