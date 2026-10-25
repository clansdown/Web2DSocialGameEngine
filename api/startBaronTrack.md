# `/api/startBaronTrack`

Opts into the baron track and captures the aspiring barony's safe name plus an
optional adult custom honor name.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "safe_barony_name_id": "stormhold",
    "honor_name": "Stormhold",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

A minor must omit `honor_name`; the server uses the safe name label.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "game_phase": "baron_track",
        "honor_name": "Stormhold",
        "safe_barony_name_id": "stormhold"
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "safe_barony_name_id required" }
{ "error": "custom_honor_name_adults_only" }
{ "error": "honor_name must be 64 characters or fewer" }
{ "error": "Must have a land patent to start the baron track" }
{ "error": "A barony with that name already exists" }
```

## Notes

- `safe_barony_name_id` must be a catalog entry from `/api/getSafeContent`.
- A minor receives the safe name in `{honor_name}` text substitutions.
- The custom honor name (adult only) is trimmed, capped at 64 characters, and
  must be unique case-insensitively against barony names.
- Transitions to `baron_track`; after all levels the player enters `baron_right`.
