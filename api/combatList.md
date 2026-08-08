# `/api/combatList`

Lists all open (lobby-phase) PvE combat matches. Intended for a future
barony-invite browser; the current scaffold uses direct code sharing instead.

**Requires authentication.**

## Request

```json
{
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
        "matches": [
            {
                "match_id": "m42",
                "match_code": "AB3K9X",
                "mode": "pve",
                "ruleset_id": "skirmish",
                "map_id": "meadow",
                "player_count": 2,
                "max_players": 8,
                "host_name": "Sir Wolf"
            }
        ]
    }
}
```

## Error Responses

```json
{ "error": "..." }
```

(Standard auth errors only.)

## Notes

- Only matches still in the lobby phase are listed; matches in countdown,
  battle, or ended phases are excluded.
