# `/api/createBarony`

Creates a new barony for a character who has completed the baron track.
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
        "barony_id": 1,
        "fiefdom_id": 5,
        "game_phase": "sandbox",
        "base_unlocked": true
    }
}
```

## Error Responses

```json
{ "error": "name required" }
{ "error": "A barony with that name already exists" }
{ "error": "Must complete the baron track to start a barony" }
```

## Notes

- Character must be in `baron_right` game phase (all 25 levels completed)
- Founder is added as `mesne_lord` role
- Barony name must be unique — checked case-insensitively, matching the
  capture-time uniqueness rule in `/api/startBaronTrack`
- Fiefdom is created with `manor_level = 1` and 5 starting gold
- `player_game_state.honor_name` is synced to the final barony name, so a name
  edited here overrides the name captured at track start
