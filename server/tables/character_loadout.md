# `character_loadout`

The **equipped cosmetics** for a character — a JSON object mapping each cosmetic
slot to an owned item id (`rts_design.md` §15.3). This is what the combat game
reads into the join-time snapshot (welcome roster + banner).

## Schema

```sql
CREATE TABLE character_loadout (
    character_id INTEGER PRIMARY KEY,
    loadout TEXT NOT NULL DEFAULT '{}',
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `character_id` | int | Owning character (primary key — one loadout row per character) |
| `loadout` | text | JSON object: slot → cosmetic id. Slots: `banner`, `knight`, `squad_uniform`, `unit`, `structure`, `flourish` |

## Relationships

- `character_id` → `characters.id`.
- Every value must be an owned (not revoked) id in `character_cosmetics`.

## Usage Notes

- Equipping validates slot fit and ownership; loadout changes take effect on the
  **next** combat match (cosmetics ride the join-time snapshot, never tick
  deltas).
- Appearance only: the loadout carries no gameplay key by construction
  (`rts_design.md` §15.5.1).
