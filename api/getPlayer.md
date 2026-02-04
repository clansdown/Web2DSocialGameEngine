# POST /api/getPlayer

Retrieve player information.

## Request

```json
{
  "player_id": 123
}
```

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "id": 123,
    "name": "Player Name",
    "level": 1
  }
}
```

### Error (400 Bad Request)

```json
{
  "error": "player_id required"
}
```

## Implementation Status

- Database query: Implemented
- Full player stats: TODO