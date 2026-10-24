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
    "avatar_safe_id": "male/3.png",
    "avatar_custom_id": null,
    "avatar_storage": "local",
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
| avatar_custom_id | string \| null | Content-addressed custom (18+) PFP key (`<id>/<sha256>.<ext>`); null when none |
| avatar_storage | string | Backend holding a custom key: `local` or `r2` |
| avatar_changed_at | integer | Unix ts of the last PFP change (24h cooldown) |
| avatar_url | string | Server-resolved PFP URL for this (self) viewer; may be `""`. Local custom storage returns `/api/avatar/<id>` (needs session credentials); R2 custom storage returns the absolute public URL. Minors always receive the safe pick |

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