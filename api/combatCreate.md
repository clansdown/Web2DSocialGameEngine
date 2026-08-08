# `/api/combatCreate`

Creates a new realtime combat match lobby. The creating character becomes the
host and first player; the returned **match code** is shared with invited
players (e.g. barony members), who join via `/api/combatJoin`. The battle
itself runs over the WebSocket route `/ws/combat` (see `docs/combat_protocol.md`).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "mode": "pve",
    "ruleset_id": "skirmish",
    "map_id": "meadow",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `character_id` | int | yes | The host character |
| `mode` | string | yes | `"pve"` only — PvP matchmaking is not implemented yet |
| `ruleset_id` | string | yes | A ruleset id from `game/config/combat/rulesets.json` |
| `map_id` | string | yes | A map name from `game/config/combat/maps/` (filename without `.json`) |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "match_id": "m42",
        "match_code": "AB3K9X",
        "mode": "pve",
        "ruleset_id": "skirmish",
        "map_id": "meadow"
    }
}
```

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "Only pve matches are available yet — pvp matchmaking is not implemented" }
{ "error": "ruleset_id required" }
{ "error": "unknown ruleset: foo" }
{ "error": "unknown map: bar" }
```

## Notes

- The player's active retinue (`retinue_members` where `status = 'active'`,
  knight first) is snapshotted into the match at creation time.
- The match lives **entirely in RAM** — nothing is written to SQLite until a
  battle ends and casualties are persisted.
- Rulesets and maps are hot-reloadable (mtime-polled) — no server restart
  needed after editing `combat/rulesets.json` or map files.
