# `/api/startBaronTrack`

Opts into the baron track — a 4x4 grid of 16 harder missions — to earn the right
to start a barony instead of joining one. Captures the aspiring barony's **honor
name** up front, which the player carries through the whole track.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "honor_name": "Stormhold",
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
        "game_phase": "baron_track",
        "honor_name": "Stormhold"
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "honor_name required" }
{ "error": "honor_name must be 64 characters or fewer" }
{ "error": "Must have a land patent to start the baron track" }
{ "error": "A barony with that name already exists" }
```

## Notes

- Character must be in `land_patent` game phase
- `honor_name` is required, trimmed, capped at 64 characters, and **must be
  unique** (case-insensitive) against existing barony names — this is a hard
  requirement, not a warning. The name is stored in `player_game_state.honor_name`
  and is reused as the prefill for `/api/createBarony`.
- Transitions to `baron_track` phase
- Client should show a 4x4 level grid
- After completing all 25 levels, the player transitions to `baron_right` phase
