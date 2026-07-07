# `/api/createDukedom`

Creates a new dukedom for a character who has completed the duke track.
Transitions the player to sandbox phase.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "name": "Stormhold",
    "description": "A fortress in the north",
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
        "dukedom_id": 1,
        "fiefdom_id": 5,
        "game_phase": "sandbox",
        "base_unlocked": true
    }
}
```

## Error Responses

```json
{ "error": "name required" }
{ "error": "A dukedom with that name already exists" }
{ "error": "Must complete the duke track to start a dukedom" }
```

## Notes

- Character must be in `duke_right` game phase (all 25 levels completed)
- Founder is added as `mesne_lord` role
- Dukedom name must be unique
- Fiefdom is created with `manor_level = 1`
