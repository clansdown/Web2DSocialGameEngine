# `/api/joinDukedom`

Joins an existing dukedom, creates a fiefdom, and transitions the player to sandbox phase.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "dukedom_id": 2,
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
        "dukedom_id": 2,
        "fiefdom_id": 5,
        "game_phase": "sandbox",
        "base_unlocked": true
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "dukedom_id required" }
{ "error": "Dukedom not found" }
{ "error": "Already a member of a dukedom" }
{ "error": "Must have a land patent to join a dukedom" }
```

## Notes

- Creates a fiefdom for the character with `manor_level = 1`
- Character must be in `land_patent` game phase
- Character can only be in one dukedom at a time
- Fiefdom position is auto-assigned
