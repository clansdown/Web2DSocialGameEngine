# POST /api/login

Authenticate a user and return all characters associated with the account.

## Request

```json
{
  "auth": {
    "username": "player_name",
    "password": "password_value"
  }
}
```

The password is only accepted on `/api/login` and `/api/createAccount`. Normal
API calls use `Authorization: Bearer <access token>`.

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "user_id": 1,
    "username": "player_name",
    "adult": false,
    "language": "en",
    "characters": [
      {
        "id": 1,
        "display_name": "CloudDragon",
        "safe_display_name": "CloudDragon",
        "level": 1,
        "avatar_safe_id": "male/3.png",
        "avatar_custom_id": null,
        "avatar_storage": "local",
        "avatar_changed_at": 0,
        "avatar_url": "/images/avatars/safe/male/3.png"
      }
    ],
    "token": "rvs_at_..."
  }
}
```

The response also sets:

```text
Set-Cookie: ravenest_refresh=rvs_rt_...;
            HttpOnly; Secure; SameSite=Lax; Path=/api/auth; Max-Age=34560000
```

| Field | Type | Description |
|-------|------|-------------|
| data.token | string | 15-minute access token; send as `Authorization: Bearer` |
| data.characters | array | Characters owned by the account |
| data.language | string | Account interface/display language |
| data.adult | boolean | Staff/credential-verified adult status |

The refresh token is never returned in the JSON body.

### Error (401 Unauthorized)

```json
{
  "error": "invalid_credentials",
  "status": "ok",
  "needs_auth": false,
  "auth_failed": true
}
```

## Implementation Notes

- Password verification uses Argon2id and runs on an HTTP worker, never the uWS loop.
- At most 5 refresh sessions are kept per account; the least-recently-used is revoked.
- A successful login creates a new `user_sessions` row and an in-memory access token.
- `WWW-Authenticate: Bearer` is set on 401 responses.
