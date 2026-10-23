# `platinum_transactions`

Idempotent purchase records for the premium store (`rts_design.md` §15.1/§15.4).
Every Platinum grant, cosmetic purchase, and bundle purchase records exactly one
row; the `idempotency_key` is what makes purchase replays safe (exactly-once
grants).

## Schema

```sql
CREATE TABLE platinum_transactions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    product_id TEXT NOT NULL,
    platinum INTEGER NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'pending',
    idempotency_key TEXT NOT NULL UNIQUE,
    created_at INTEGER NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `id` | int | Primary key |
| `character_id` | int | Owning character |
| `product_id` | text | Product / cosmetic / bundle id involved |
| `platinum` | int | Platinum delta: positive for a pack grant, negative for a cosmetic debit |
| `status` | text | `pending` (real-money intent) or `settled` |
| `idempotency_key` | text | Client-supplied unique key; `UNIQUE` enforces exactly-once |
| `created_at` | int | Unix timestamp |

## Relationships

- `character_id` → `characters.id`.
- `product_id` refers to an id in `game/config/cosmetics.json` (items, bundles,
  or products).

## Usage Notes

- `purchasePremiumItem` writes a **settled** row and debits Platinum.
- `purchasePlatinum` writes a **pending** row; `settlePlatinum` flips it to
  `settled` and grants the Platinum (idempotent).
- A duplicate `idempotency_key` is detected before any charge/grant.
- Index: `idx_platinum_transactions_character`.
