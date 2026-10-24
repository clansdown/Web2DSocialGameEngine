# POST /api/login

Authenticate a user and return all characters associated with the account.

## Request

```json
{
  "username": "player_name",
  "password": "password_value"
}
```

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
    ]
  },
  "token": "hex_token_string"
}
```

| Field | Type | Description |
|-------|------|-------------|
| user_id | integer | User account ID |
| username | string | Login username |
| adult | boolean | Whether account is for adults |
| language | string | Account interface / display language (chosen at creation) |
| characters | array | Array of character objects owned by this user |
| characters[].id | integer | Character ID |
| characters[].display_name | string | Character display name |
| characters[].safe_display_name | string | Character safe display name |
| characters[].level | integer | Character level |
| characters[].avatar_safe_id | string \| null | Safe-pool PFP id |
| characters[].avatar_custom_id | string \| null | Content-addressed custom PFP key, or null |
| characters[].avatar_storage | string | `local` or `r2` |
| characters[].avatar_changed_at | integer | Unix ts of the last PFP change (24h cooldown) |
| characters[].avatar_url | string | Server-resolved PFP URL for this viewer (safe pick for minors) |
| token | string | Authentication token (included if password used) |

### Error (400 Bad Request)

```json
{
  "error": "Invalid username or password"
}
```

## Implementation Notes

- Queries users table to verify credentials and get user_id
- Queries characters table by user_id to retrieve all characters
- Returns array of characters to support multiple characters per user
- Token is generated when password authentication succeeds

## Implementation Status

- Database queries: Implemented
- Password hashing: Implemented
- Session token generation: Implemented