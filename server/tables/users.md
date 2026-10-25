# users Table

Stores user account information and authentication credentials.

## Schema

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    adult INTEGER NOT NULL DEFAULT 0,
    adult_verified_at INTEGER NOT NULL DEFAULT 0,
    adult_verified_by INTEGER,
    adult_verification_method TEXT,
    language TEXT NOT NULL DEFAULT 'en',
    social_restricted INTEGER NOT NULL DEFAULT 0,
    support_locked INTEGER NOT NULL DEFAULT 0,
    restriction_updated_at INTEGER NOT NULL DEFAULT 0,
    invitation_code_id TEXT
);
```

## Fields

| Field | Type | Constraints | Purpose |
|-------|------|-------------|---------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | Unique user identifier |
| username | TEXT | UNIQUE NOT NULL | Login username (must be unique) |
| password_hash | TEXT | NOT NULL | Hashed password (yescrypt) |
| created_at | INTEGER | NOT NULL | Unix timestamp of account creation |
| adult | INTEGER | NOT NULL DEFAULT 0 | Account-level adult status (1=true, 0=false/unknown). Set only by Digital Credentials or admin/CS. |
| adult_verified_at | INTEGER | NOT NULL DEFAULT 0 | Unix ts of the last adult-status verification/change |
| adult_verified_by | INTEGER | NULLABLE | Admin account id that made the change (NULL for self-service Digital Credentials) |
| adult_verification_method | TEXT | NULLABLE | `digital_credential`, `customer_service`, or similar audit value |
| language | TEXT | NOT NULL DEFAULT 'en' | Chosen interface / display language (also the default chat translation target) |
| social_restricted | INTEGER | NOT NULL DEFAULT 0 | Staff-set social restriction: blocks chat/DMs/friends/groups/barony joins; solo play remains |
| support_locked | INTEGER | NOT NULL DEFAULT 0 | Staff-set support lock: read tickets/notifications only; staff can still send a final message |
| restriction_updated_at | INTEGER | NOT NULL DEFAULT 0 | Unix ts of the last account-restriction change |
| invitation_code_id | TEXT | NULLABLE, UNIQUE | The `invites.db` invitation row redeemed for this account. NULL for `off`/global-code registrations. The unique index is the single-use race guard. |

## Indexes

- Unique index on `username`
- Unique index `idx_users_invitation_code_id` on `invitation_code_id` (multiple NULLs allowed)

## Relationships

- One-to-many with `characters` via `user_id` foreign key

## Notes

- Password hashing using glibc `crypt()` with yescrypt prefix `$y$`
- `adult` is account-level (the human), never per-character. It gates every
  custom-content write/read path; see `docs/minor_content_visibility.md`.
- There is no client-callable age override. Admin/CS changes go through
  `/api/adminSetAdultStatus` and are audited.
- `language` is chosen at first launch and persisted at account creation; the
  client adopts it on every device and the server uses it as the display
  language for chat translation (see `server/docs/chat.md`)
- `invitation_code_id` is the redemption ledger for individual invitation
  codes. The referenced row lives in the separate, lazily-opened `invites.db`;
  see `server/tables/invitation_codes.md` and `docs/registration.md`.
- Accessed by `/api/login`, `/api/createAccount`, `/api/updateUserProfile` endpoints