# `character_monthly_pass_log`

Append-only history of every monthly-pass entitlement transition. Rows are
**never updated or deleted**: grants, revocations, and undo restores all add a
new row. The current state lives in `character_monthly_pass`.

## Schema

```sql
CREATE TABLE character_monthly_pass_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id INTEGER NOT NULL,
    event TEXT NOT NULL,
    source TEXT NOT NULL,
    months INTEGER NOT NULL DEFAULT 0,
    permanent INTEGER NOT NULL DEFAULT 0,
    expires_at INTEGER NOT NULL DEFAULT 0,
    reason TEXT NOT NULL DEFAULT '',
    staff_account_id INTEGER NOT NULL DEFAULT 0,
    product_id TEXT NOT NULL DEFAULT '',
    reference_id TEXT NOT NULL DEFAULT '',
    created_at INTEGER NOT NULL,
    FOREIGN KEY(character_id) REFERENCES characters(id)
);
```

## Fields

| Field | Type | Purpose |
|---|---|---|
| `id` | int | Primary key |
| `character_id` | int | Character whose entitlement changed |
| `event` | text | `grant`, `revoke`, or `restore` |
| `source` | text | `staff_grant`, `staff_revoke`, `staff_undo`, `purchase`, or `promo` |
| `months` | int | Months added by a grant; `0` for revoke/restore/permanent |
| `permanent` | int | Resulting permanent flag |
| `expires_at` | int | Resulting expiry; `0` when absent/permanent |
| `reason` | text | Staff-entered reason, or a purchase description |
| `staff_account_id` | int | Acting staff account; `0` for purchases/promos |
| `product_id` | text | Future purchase product id |
| `reference_id` | text | Staff action id, or purchase idempotency key |
| `created_at` | int | Unix timestamp |

## Event Sources

| Event | Source | Meaning |
|---|---|---|
| `grant` | `staff_grant` | Staff added months or made the pass permanent |
| `revoke` | `staff_revoke` | Staff removed the pass |
| `restore` | `staff_undo` | An undo restored a prior row (active, permanent, or expired) |
| `revoke` | `staff_undo` | An undo removed a pass where no prior row existed |
| `grant` | `purchase` | Future real-money purchase (not wired yet) |
| `grant` | `promo` | Future promotional grant |

## Indexes

- `idx_character_monthly_pass_log_character` on
  `(character_id, created_at DESC)`.

## Relationships

- `character_id` → `characters.id`.
- `staff_account_id` → `users.id` when the source is a staff action.
- `reference_id` points at `staff_action_journal.id` for staff actions, or the
  purchase `idempotency_key` for future purchases.

## Usage Notes

- The log is append-only by design. Revokes and undos add rows; they do not
  rewrite the original grant.
- The staff account view returns the latest 20 rows per character.
- Reasoning for the log and the 31-day month rule: see
  `server/docs/staff_console.md` and `docs/staff_authorization.md`.
