# `/api/setCharacterArchetype`

Sets a character's starting path (archetype), which determines which mini-game they play.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "archetype": "wolf_warden",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `character_id` | integer | ID of the character to update |
| `archetype` | string | Must be `"wolf_warden"` or `"assarter"` |
| `auth` | object | Authentication credentials |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "id": 1,
        "display_name": "CloudDragon",
        "safe_display_name": "CloudDragon",
        "level": 1,
        "archetype": "wolf_warden"
    }
}
```

## Error Responses

```json
{ "status": "ok", "error": "character_id required" }
{ "status": "ok", "error": "archetype must be 'wolf_warden' or 'assarter'" }
{ "status": "ok", "error": "Character does not belong to this user" }
```

## Notes

- Archetype is stored in the `characters` table's `archetype` column
- Can only be set once — NULL means unset, otherwise the path is chosen
- The client uses this to determine which mini-game to show:
  - `wolf_warden` → tower defense
  - `assarter` → weeding
