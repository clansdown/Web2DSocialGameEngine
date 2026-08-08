# `/api/combatGetConfigs`

Returns the available combat **rulesets** and **maps** for the match-creation
lobby. Reads from the hot-reloadable configs (`combat/rulesets.json` and the
`combat/maps/` directory), so new content appears without a server restart.

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
        "rulesets": [
            { "id": "skirmish", "name": "Skirmish", "mode": "pve" },
            { "id": "scrimmage", "name": "Scrimmage", "mode": "pvp" }
        ],
        "maps": [
            { "id": "meadow", "file": "meadow.json" }
        ]
    }
}
```

## Error Responses

```json
{ "error": "..." }
```

(Standard auth errors only.)
