# POST /api/login

Authenticate a user and return player data.

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
    "id": 1,
    "username": "player_name"
  }
}
```

### Error (400 Bad Request)

```json
{
  "error": "Invalid username or password"
}
```

## Implementation Status

- Database queries: Implemented
- Password hashing: TODO
- Session token generation: TODO