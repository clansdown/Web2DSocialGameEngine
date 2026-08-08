# `/api/getRetinue`

Returns the character's full **retinue** — the knight (the character itself)
plus every unit they have mustered. Units are created by the manor game's
`train_troops` action (currently a stub); the knight row is auto-created on
first access. Combat matches snapshot the *active* members when a player
joins (see `docs/combat_protocol.md` for the snapshot shape in `welcome`).

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
        "members": [
            {
                "id": 1,
                "character_id": 1,
                "display_name": "Sir Wolf",
                "unit_class": "knight",
                "is_knight": true,
                "level": 1,
                "weapons": {},
                "armor": {},
                "abilities": [],
                "status": "active",
                "created_at": 1750000000
            }
        ]
    }
}
```

## Error Responses

```json
{ "error": "Character does not belong to this user" }
```

## Notes

- `status` is `active` | `casualty` | `retired`. Casualties are written after
  a combat match ends (rulesets with permanent death); they can be revived by
  future manor-game mechanics.
- Schema documented in `server/tables/retinue_members.md`.
