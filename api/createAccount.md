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
| adult | boolean | Yes | Whether account is for adults (enables custom displayName) |
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
    "user_id": 1,
    "username": "new_player",
    "adult": false,
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
|-------|------|-------------|
| user_id | integer | User account ID |
| username | string | Login username |
| adult | boolean | Whether account is for adults |
| characters | array | Array of character objects (first character is auto-created) |
| characters[].id | integer | Character ID |
| characters[].display_name | string | Character display name |
| characters[].safe_display_name | string | Character safe display name |
| characters[].level | integer | Character level (default: 1) |
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
- User account created with provided username, password hash, adult flag
- Character created with auto-generated safe_display_name (word1+word2)
- If adult=true, display_name uses custom displayName; otherwise uses safe_display_name
- Default character created with level 1
- Token generated immediately using SHA256 algorithm
- User can immediately use the token for authenticated requests
- `safe_display_name` is created by concatenating word1 and word2
- If the combination already exists, a number is appended (e.g., "CloudDragon2")
- `display_name` is only set to custom value if adult=true

## Implementation Status

- Implemented