# Auth session endpoints

All routes are `POST` except `GET /api/auth/sessions`.

## POST /api/auth/refresh

Public; authenticates with the `ravenest_refresh` cookie only.

Request body: `{}` (credentials are the cookie).

Success: the same payload as `/api/login`, including a fresh `data.token`.
Sets a re-issued sliding `ravenest_refresh` cookie.

Failure (401): `{"error":"invalid_refresh_token"}` and clears the cookie.
Transient failures (network errors, 429, 5xx) should be retried; clients must
not treat them as a revoked session.

## POST /api/auth/logout

Public; reads the refresh cookie, revokes that session, and clears the cookie.

Success: `{"logged_out":true}`.

## POST /api/auth/logoutAll

Requires `Authorization: Bearer <access token>`. Revokes every session for the
account and clears the cookie.

## POST /api/auth/changePassword

Requires Bearer. Body:

```json
{ "current_password": "...", "new_password": "..." }
```

Verifies the current password, stores an Argon2id hash, and revokes every other
session. The current session remains valid.

## GET /api/auth/sessions

Requires Bearer. Returns:

```json
{
  "sessions": [
    {
      "session_id": "uuid",
      "created_at": 1710000000,
      "last_used_at": 1710000300,
      "user_agent": "...",
      "ip": "...",
      "current": true
    }
  ]
}
```

## POST /api/auth/revokeSession

Requires Bearer. Body: `{"session_id":"uuid"}`. The session must belong to the
account. Revoking the current session also clears the cookie.

## Errors

All auth failures return HTTP 401 with `WWW-Authenticate: Bearer`, unless the
request was rate-limited (429) or the worker queue is full (503 `server_busy`).
