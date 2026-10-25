# `/api/createBarony`

Creates a new barony for a character who has completed the baron track.
Transitions the player to sandbox phase.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "safe_name_id": "stormhold",
    "safe_tag_ids": ["laid_back", "pve_focused"],
    "name": "Stormhold",
    "description": "A fortress in the north",
    "accepts_minors": true,
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

`name` and `description` are adult-only custom text. A minor must omit them;
the server uses the safe name/tags and forces `accepts_minors = true`.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "barony_id": 1,
        "fiefdom_id": 5,
        "game_phase": "sandbox",
        "base_unlocked": true,
        "name": "Stormhold",
        "safe_name_id": "stormhold",
        "accepts_minors": true
    }
}
```

## Error Responses

```json
{ "error": "safe_name_id required" }
{ "error": "invalid_safe_barony_tag" }
{ "error": "custom_name_adults_only" }
{ "error": "A barony with that name already exists" }
{ "error": "Must complete the baron track to start a barony" }
```

## Notes

- `safe_name_id` and `safe_tag_ids` are mandatory server-owned picks from
  `/api/getSafeContent`.
- A barony's safe name is unique among active baronies; its display name
  remains unique case-insensitively (tombstoned names stay reserved).
- A minor founder cannot provide custom text and is forced to accept minors.
- An existing fiefdom is reused; a new one is created only if the character has
  none.
