# POST /api/createAccount

Create a new user account and automatically log in.

> Account creation always starts **non-adult**. New characters use their safe
> display name. Custom names and 18+ status are post-creation Profile actions.
> See `docs/adult_verification.md` for the deferred self-service verification
> design.

## Request

```json
{
  "username": "new_player",
  "password": "password_value",
  "word1": "Cloud",
  "word2": "Dragon",
  "sex": "female",
  "language": "en",
  "invitation_code": "ABCD-EFGH-JKLM"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| username | string | Yes | Login username (case-insensitively unique) |
| password | string | Yes | Account password (minimum 8 characters, client-enforced) |
| word1 | string | Yes | First word from `safe_words_1.txt` |
| word2 | string | Yes | Second word from `safe_words_2.txt` |
| sex | string | No | Character sex: `"male"` or `"female"` for gender-substituted text |
| language | string | No | Chosen interface / display language (default `"en"`); also the default chat translation target |
| invitation_code | string | Conditional | Required when the server is in `global_code` or `invite_only` registration mode. Ignored in `off` mode. Dashes/spaces are optional. |

`word1` must exist in the configured `safe_words_1.txt` and `word2` must exist
in `safe_words_2.txt`.

No authentication is required.

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "user_id": 1,
    "username": "new_player",
    "adult": false,
    "language": "en",
    "characters": [
      {
        "id": 1,
        "display_name": "CloudDragon",
        "safe_display_name": "CloudDragon",
        "level": 1
      }
    ],
    "token": "hex_token_string"
  }
}
```

| Field | Type | Description |
|---|---|---|
| user_id | integer | New account id |
| username | string | Created username |
| adult | boolean | Always `false` at creation |
| language | string | Persisted interface / display language |
| characters | array | New character list (one character) |
| token | string | Access token for immediate authenticated requests |

### Errors

```json
{
  "status": "ok",
  "data": {},
  "error": "username and password required"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "word1 and word2 required for safe display name"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "Invalid word1 or word2 - words must exist in safe word lists"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "Username already exists"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "invalid_invitation_code"
}
```

```json
{
  "status": "ok",
  "data": { "retry_after_seconds": 600 },
  "error": "rate_limited"
}
```

## Implementation Notes

- Passwords are hashed with Argon2id.
- The account is created non-adult; `adult` in the response is always `false`.
- The character's `safe_display_name` is built from `word1 + word2`. If the
  combination is taken, a unique suffix is appended (for example
  `CloudDragon-2`).
- The visible `display_name` starts equal to `safe_display_name`.
- A custom display name is set later via `/api/updateCharacterProfile`
  (`display_name` / `clear_display_name`), and 18+ status is requested from the
  Profile activity.
- The response token is generated immediately and can be used for authenticated
  requests.
- The account-creation rate limiter is config-driven and in-memory. It applies
  in every registration mode, including `off`.

## Implementation Status

- Implemented
