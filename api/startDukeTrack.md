# `/api/startDukeTrack`

Opts into the duke track — a 4x4 grid of 16 harder missions — to earn the right
to start a dukedom instead of joining one.

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
        "game_phase": "duke_track"
    }
}
```

## Error Responses

```json
{ "error": "Must have a land patent to start the duke track" }
```

## Notes

- Character must be in `land_patent` game phase
- Transitions to `duke_track` phase
- Client should show a 4x4 level grid
- After completing all 25 levels, the player transitions to `duke_right` phase
