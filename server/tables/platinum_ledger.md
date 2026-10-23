# `platinum_ledger`

The **premium currency** balance for a character. Platinum is a separate ledger,
**completely non-fungible** with `gold`/`silver_pence` (`rts_design.md` §15.1):
no code path converts between them, and no economy code reads Platinum.

## Schema

```sql
CREATE TABLE platinum_ledger (
    character_id INTEGER PRIMARY KEY,
    balance INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `character_id` | int | Owning character (primary key — one ledger row per character) |
| `balance` | int | Platinum balance; only ever changed by Platinum purchases/settlements/refunds |

## Relationships

- One row per `characters.id` (created lazily on first credit via upsert).
- Written only by `cosmetics_db` (see `server/CosmeticsDB.cpp`).

## Usage Notes

- **Variety only:** Platinum can buy cosmetics, maps, a monthly pass, and
  (deferred) adventure access — never power, XP, or economy advantage (§15.0).
- Minted only by a settled `platinum_transactions` row; spent only by
  `/api/purchasePremiumItem`.
- Not tradable, not giftable, not obtainable from normal gameplay.
