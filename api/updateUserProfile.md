# POST /api/updateUserProfile

Update user account profile settings including adult status.

## Request

```json
{
  "auth": {
    "username": "player_name",
    "token": "xxx"
  },
  "adult": true
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| auth | object | Yes | Authentication object (see below) |
| adult | boolean | Yes | Change adult status |

**Authentication object:**

| Field | Type | Description |
|-------|------|-------------|
| username | string | Account username |
| token | string | Authentication token (alternative to password) |

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "adult": true,
    "token": "hex_token_string"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| adult | boolean | Current adult status |
| token | string | Updated authentication token |

### Error (400 Bad Request)

```json
{
  "status": "ok",
  "data": {},
  "error": "authentication required"
}
```

```json
{
  "status": "ok",
  "data": {},
  "error": "adult field required"
}
```

## Implementation Notes

- Updates the `adult` flag on the user account
- Adult flag controls whether custom display names can be set on characters
- Minor accounts (adult=false) cannot have custom display names on their characters
- Token refresh is included in response

## Implementation Status

- Implemented