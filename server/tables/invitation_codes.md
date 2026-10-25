# invitation_codes Table (`invites.db`)

Single-use registration invitation codes. This table lives in its own
**lazily-opened** database (`invites.db`) so the whole invitation feature can be
retired by switching `game/config/registration.json` to `mode: "off"`, stopping
the server, and deleting the file.

## Schema

```sql
CREATE TABLE invitation_codes (
    id TEXT PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    created_by_account_id INTEGER NOT NULL,
    created_by_kind TEXT NOT NULL,        -- 'staff' | 'player'
    created_at INTEGER NOT NULL,
    used_at INTEGER,
    used_by_account_id INTEGER UNIQUE,    -- nullable; many NULLs allowed
    revoked_at INTEGER,
    revoked_by_account_id INTEGER
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | TEXT | PRIMARY KEY | Random `inv_<timestamp>_<random>` id, never auto-increment |
| code | TEXT | UNIQUE NOT NULL | Canonical 12-char uppercase Crockford-style code, no dashes |
| created_by_account_id | INTEGER | NOT NULL | Account that generated the code |
| created_by_kind | TEXT | NOT NULL | `staff` or `player` |
| created_at | INTEGER | NOT NULL | Unix timestamp (UTC) |
| used_at | INTEGER | NULLABLE | Set when redeemed |
| used_by_account_id | INTEGER | UNIQUE, NULLABLE | Account created with this code; one account per code |
| revoked_at | INTEGER | NULLABLE | Staff revocation timestamp |
| revoked_by_account_id | INTEGER | NULLABLE | Staff account that revoked it |

## Indexes

- `code` is `UNIQUE`.
- `used_by_account_id` is `UNIQUE` (SQLite allows multiple `NULL`s).
- `idx_invitation_codes_creator_created` on `(created_by_account_id, created_at)`
  — daily/lifetime player generation counts.
- `idx_invitation_codes_created` on `(created_at)`.
- `idx_invitation_codes_used_by` on `(used_by_account_id)` (also covered by the
  inline UNIQUE constraint).

## Lifecycle

1. Staff (in `invite_only` mode) or a player with allowance generates a code.
2. A new account submits it to `/api/createAccount`.
3. The server inserts the `game.db` user with `users.invitation_code_id` equal
   to this row's `id`; the unique index on that column is the hard single-use
   race guard.
4. After the game transaction commits, the server sets `used_at` /
   `used_by_account_id` here. If that update fails, the game ledger still makes
   the code unusable.

## Notes

- Codes are plaintext. There is no expiry.
- `global_code` mode accepts the configured global code **and** any still-valid
  individual codes from this table. `off` ignores codes entirely and never
  opens this database.
- When deleting `invites.db`, `users.invitation_code_id` can optionally be
  cleared (`UPDATE users SET invitation_code_id = NULL;`). Random text ids mean
  a recreated database cannot collide with dangling ledger values, but clearing
  is still the cleanest teardown.
