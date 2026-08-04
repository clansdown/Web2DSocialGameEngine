# `/api/joinBarony`

Joins an existing barony, creates a fiefdom, and transitions the player to sandbox phase.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
        "barony_id": 2,
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
    "barony_id": 2,
        "fiefdom_id": 5,
        "game_phase": "sandbox",
        "base_unlocked": true
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "barony_id required" }
{ "error": "Barony not found" }
{ "error": "Already a member of a barony" }
{ "error": "Must have a land patent to join a barony" }
```

## Notes

- Creates a fiefdom for the character with `manor_level = 1` and 5 starting gold
- Character must be in `land_patent` game phase
- Character can only be in one barony at a time
- Fiefdom position is auto-assigned
