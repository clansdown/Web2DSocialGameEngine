# user_sessions

Long-lived refresh sessions. Access tokens are in memory only and are not stored here.

**Database:** `ravenest_game`

## Schema

```sql
CREATE TABLE user_sessions (
    id UUID PRIMARY KEY,
    account_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    refresh_token_hash TEXT NOT NULL UNIQUE,
    created_at BIGINT NOT NULL,
    last_used_at BIGINT NOT NULL,
    revoked_at BIGINT,
    revoked_reason TEXT,
    user_agent TEXT,
    ip TEXT
);
CREATE INDEX user_sessions_account_active_idx
    ON user_sessions (account_id, revoked_at);
CREATE INDEX user_sessions_last_used_idx
    ON user_sessions (account_id, last_used_at);
```

## Fields

| Column | Type | Notes |
|---|---|---|
| `id` | UUID | Session id; also used for the in-memory snapshot key |
| `account_id` | BIGINT | Owning account |
| `refresh_token_hash` | TEXT | SHA-256 hex of `rvs_rt_...`; the plaintext token is only in the cookie |
| `created_at` | BIGINT | Unix seconds |
| `last_used_at` | BIGINT | Refresh time; drives LRU eviction |
| `revoked_at` | BIGINT | Null while active |
| `revoked_reason` | TEXT | `logout`, `logout_all`, `password_change`, `adult_status_change`, `session_limit`, `user_revoked`, ... |
| `user_agent` / `ip` | TEXT | Display metadata for the sessions screen |

## Rules

- At most **5 active** sessions per account.
- A sixth login revokes the least-recently-used active session.
- Refresh tokens do not rotate; refresh updates `last_used_at`.
- Password change revokes all sessions except the current one.
- Adult-status change and account lock revoke all sessions.
- Access tokens are 15-minute in-memory entries and disappear on restart.
