# POST /api/createAccount

Create a new user account and automatically log in.

## Request

```json
{
  "username": "new_player",
  "password": "password_value",
  "adult": false,
  "word1": "Cloud",
  "word2": "Dragon",
  "displayName": "CoolPlayer123"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| username | string | Yes | Login username (must be unique) |
| password | string | Yes | Account password |
| adult | boolean | Yes | Whether account is for adults (enables displayName) |
| word1 | string | Yes | First word from safe_words_1.txt |
| word2 | string | Yes | Second word from safe_words_2.txt |
| displayName | string | No | Custom display name (only allowed if adult=true) |

**Note:** `word1` must exist in `config/safe_words_1.txt` and `word2` must exist in `config/safe_words_2.txt`. The server looks for these files in its working directory.

No authentication required for this endpoint.

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "id": 1,
    "username": "new_player",
    "player_id": 1,
    "adult": false,
    "displayName": "",
    "safeDisplayName": "CloudDragon",
    "token": "hex_token_string"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | integer | User account ID |
| username | string | Login username |
| player_id | integer | Default player character ID |
| adult | boolean | Whether account is for adults |
| displayName | string | Custom display name (empty if adult=false) |
| safeDisplayName | string | Generated unique safe name (word1+word2) |
| token | string | Authentication token for subsequent requests |

### Error (400 Bad Request)

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
  "error": "Username already exists"
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
  "error": "displayName can only be set if adult is true"
}
```

## Implementation Notes

- Password hashed using glibc `crypt()` with yescrypt prefix `$y$`
- Random salt appended to `$y$` prefix before hashing
- Default player created automatically with level 1
- Token generated immediately using SHA256 algorithm (same as `/api/login`)
- User can immediately use the token for authenticated requests
- `safeDisplayName` is created by concatenating word1 and word2
- If the combination already exists, a number is appended (e.g., "CloudDragon2")
- `displayName` is only stored and returned if `adult=true`

## Implementation Status

- Implemented