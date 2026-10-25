# POST /api/getCharacter

Retrieve a viewer-relative character profile. Minors receive the safe display
name and safe avatar for every character; adults receive custom content when it
exists.

## Request

```json
{
  "character_id": 123,
  "auth": { "username": "alice", "token": "..." }
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
    "safe_display_name": "QuietOtter",
    "level": 1,
    "archetype": null,
    "sex": "male",
    "avatar_url": "/images/avatars/safe/male/3.png"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| id | integer | Character ID |
| display_name | string | Viewer-relative name (safe for minors) |
| safe_display_name | string | Always-safe name |
| level | integer | Character level |
| archetype | string \| null | Starting path; null until chosen |
| sex | string \| null | `"male"` / `"female"`; null until chosen |
| avatar_url | string | Viewer-relative PFP URL; may be `""` |

### Error (400 Bad Request)

```json
{
  "error": "character_id required"
}
```

## Notes

- The server never returns `avatar_custom_id` or `avatar_storage` to another
  player. Custom R2 keys are server-only.
- A minor always receives the safe name and safe portrait.
- Requesting a missing character returns `character not found`.
