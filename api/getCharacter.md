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
    "level": 1,
    "archetype": null,
    "sex": "male",
    "avatar_safe_id": "3.png",
    "avatar_custom_id": null,
    "avatar_changed_at": 1750006500,
    "avatar_url": "/images/avatars/safe/3.png"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Character ID |
| display_name | string | Character display name |
| safe_display_name | string | Character safe display name |
| level | integer | Character level |
| archetype | string \| null | Starting path; null until chosen |
| sex | string \| null | `"male"` / `"female"`; null until chosen |
| avatar_safe_id | string \| null | Safe-pool PFP id; null until the creation picker is answered |
| avatar_custom_id | string \| null | Custom (18+) PFP filename; null when none |
| avatar_changed_at | integer | Unix ts of the last PFP change (24h cooldown) |
| avatar_url | string | Server-resolved PFP URL for this (self) viewer; may be `""`. A custom image is returned as `/api/avatar/<id>` and needs session credentials |

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