# `/api/startBaronTrack`

Opts into the baron track — a 4x4 grid of 16 harder missions — to earn the right
to start a barony instead of joining one.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
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
        "game_phase": "baron_track"
    }
}
```

## Error Responses

```json
{ "error": "Must have a land patent to start the baron track" }
```

## Notes

- Character must be in `land_patent` game phase
- Transitions to `baron_track` phase
- Client should show a 4x4 level grid
- After completing all 25 levels, the player transitions to `baron_right` phase
