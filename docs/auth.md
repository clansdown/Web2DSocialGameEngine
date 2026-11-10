# Authentication and sessions

## Passwords

- **Argon2id** PHC strings (`server/auth/Argon2.cpp`), 64 MiB, 3 iterations, 1 lane.
- Unknown usernames run a dummy Argon2id verification so login timing does not reveal account existence.
- Hashing/verification runs on an HTTP worker; it never blocks the uWS loop.
- Legacy SHA-512 hashes are not accepted; there is no SQLite data migration.

## Tokens

```text
access token:   rvs_at_<base64url(32 random bytes)>    15 minutes, in memory
refresh token:  rvs_rt_<base64url(32 random bytes)>    long-lived, no rotation
session id:     UUID v4
```

- Access token hashes live in `auth::SessionManager` in memory.
- Refresh token hashes live in `ravenest_game.user_sessions.refresh_token_hash`.
- Only SHA-256 hashes are stored; full tokens are never logged.
- Access tokens are validated without a database call.

## Sessions

- Multiple sessions per account.
- At most **5 active sessions**; the oldest `last_used_at` session is revoked when the cap is reached.
- Refresh updates `last_used_at` and re-sets the cookie (sliding `Max-Age`).
- Refresh tokens do not rotate; revoke on password change, adult-status change, account lock, or logout-all.

## Cookie

```text
Set-Cookie: ravenest_refresh=<token>;
            HttpOnly;
            Secure;
            SameSite=Lax;
            Path=/api/auth;
            Max-Age=34560000
```

- Host-only on the API host; no `Domain`.
- `Secure` is disabled only when `RAVENEST_COOKIE_SECURE=0` (local HTTP development).
- Auth responses set `Cache-Control: no-store`.

## REST surface

```text
POST /api/login                 body: {auth:{username,password}}
POST /api/createAccount         public; sets refresh cookie
POST /api/auth/refresh          cookie only; returns user/characters + access token
POST /api/auth/logout           cookie only; revokes the refresh session
POST /api/auth/logoutAll        Bearer; revokes every session
POST /api/auth/changePassword   Bearer; revokes all other sessions
GET  /api/auth/sessions         Bearer
POST /api/auth/revokeSession    Bearer
```

Normal endpoints use:

```http
Authorization: Bearer rvs_at_...
```

The old body `auth:{username,token}` form is accepted for transitional clients. Passwords are never accepted on normal endpoints.

## Errors

```text
401 token_missing
401 token_expired
401 token_invalid
401 invalid_credentials
403 origin_not_allowed
429 rate_limited
503 server_busy
```

`401` responses include `WWW-Authenticate: Bearer`.

## Revocation triggers

- logout: one session
- logout-all / password change: all sessions (password change keeps the current one)
- adult-status change: all sessions (forces re-login)
- account lock/ban: all sessions
- session cap: least-recently-used
- character create: update `character_ids` in the in-memory account snapshots

## Client

- Access token exists only in JS memory.
- Refresh uses `fetch(..., {credentials:'include'})` against `/api/auth/refresh`.
- Transient refresh failures (network/5xx/429) retry with backoff; only a
  definitive `401 invalid_refresh_token` clears the local session and returns
  the player to login.
- Startup restores the session by refreshing, not by replaying a stored password.
- Legacy OPFS password keys are deleted; no credential is written to disk.
- `with_avatar_auth` only appends query credentials for the legacy bare
  `/api/avatar/<id>` route. New local avatars use an unguessable
  `/api/avatar/<id>/<content-key>` capability URL; R2 avatars are capability
  URLs too.

## Deployment note

In-memory access tokens are per-process. Pre-alpha runs a single server process. Multi-process deployments need a shared token/session layer (e.g. Redis) or sticky sessions; this is out of scope for Phase 1.
