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
  capture-time uniqueness rule in `/api/startBaronTrack`. Tombstoned (defunct)
  barony names still count as taken.
- An existing fiefdom is **reused**; a new one is created only if the character
  has none (one fiefdom per character). New fiefdoms start at `manor_level = 0`
  (home_base under construction) with the `economy.json.starting_resources`
  balance.
- `player_game_state.honor_name` is synced to the final barony name, so a name
  edited here overrides the name captured at track start
