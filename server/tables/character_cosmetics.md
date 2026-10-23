# `character_cosmetics`

The set of cosmetics a character **owns**. Ownership is **character-bound**
(`rts_design.md` §15.3), so each character has its own collection.

## Schema

```sql
CREATE TABLE character_cosmetics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    cosmetic_id TEXT NOT NULL,
    source TEXT NOT NULL DEFAULT 'store',
    acquired_at INTEGER NOT NULL,
    revoked INTEGER NOT NULL DEFAULT 0,
    UNIQUE(character_id, cosmetic_id),
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `id` | int | Primary key |
| `character_id` | int | Owning character |
| `cosmetic_id` | text | Item id in `game/config/cosmetics.json` |
| `source` | text | `store` or `adventure_reward` (deferred reward seam) |
| `acquired_at` | int | Unix timestamp |
| `revoked` | int | 1 after a refund; the item is visually locked and cleared from loadouts |

## Relationships

- `character_id` → `characters.id`.
- The equipped subset lives in `character_loadout`.
- `UNIQUE(character_id, cosmetic_id)` makes grants idempotent.

## Usage Notes

- Refunds **revoke** (set `revoked = 1`) rather than deleting the row
  (`rts_design.md` §15.4/§15.8); re-granting flips `revoked` back to 0.
- Index: `idx_character_cosmetics_character`.
- `cosmetic_id` values are validated against the whitelist-validated catalog, so
  a cosmetic can never reference a gameplay key.
