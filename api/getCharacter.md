# POST /api/getCharacter

Retrieve character information.

## Request

```json
{
  "character_id": 123
}
```

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "id": 123,
    "display_name": "CloudDragon",
    "safe_display_name": "CloudDragon",
    "level": 1
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Character ID |
| display_name | string | Character display name |
| safe_display_name | string | Character safe display name |
| level | integer | Character level |

### Error (400 Bad Request)

```json
{
  "error": "character_id required"
}
```

## Implementation Notes

- Queries characters table by character_id
- Returns full character information including display names and level

## Implementation Status

- Database query: Implemented
- Full character stats: TODO