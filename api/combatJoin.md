# `/api/combatJoin`

Joins an existing PvE combat lobby by its shareable **match code** (from
`/api/combatCreate`). Used for barony-invite play: any invited player with the
code can join while the match is in the lobby phase.

**Requires authentication.**

## Request

```json
{
    "character_id": 2,
    "match_code": "AB3K9X",
    "auth": {
        "username": "ally",
        "token": "session_token_hex"
    }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `character_id` | int | yes | The joining character |
| `match_code` | string | yes | 6-character lobby code (case-insensitive) |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "match_id": "m42",
        "match_code": "AB3K9X",
        "mode": "pve"
    }
}
```

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "match_code required" }
{ "error": "match not found" }
{ "error": "already in match" }
{ "error": "match is full or has started" }
```

## Notes

- The joining player's active retinue is snapshotted into the match.
- Team assignment is automatic: PvE places everyone on team 1.
- After joining, open the WebSocket to `/ws/combat` and authenticate
  (`auth` first message) to receive the `welcome` + full state snapshot.
