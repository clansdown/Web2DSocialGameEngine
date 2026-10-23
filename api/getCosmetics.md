# `/api/getCosmetics`

Returns the character's cosmetic state: the owned set, the equipped **loadout**
(slot → cosmetic id), and the Platinum balance. The loadout is what rides the
combat join-time snapshot (`welcome` roster + banner) — see
`docs/combat_protocol.md`.

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
        "owned": ["banner_emberfield", "uniform_muster_green"],
        "loadout": { "banner": "banner_emberfield", "knight": "knight_grey_plate" },
        "platinum": 50
    }
}
```

| Field | Type | Description |
|---|---|---|
| `owned` | string[] | Cosmetic ids owned by this character (not revoked) |
| `loadout` | object | Equipped slot → cosmetic id (banner/knight/squad_uniform/unit/structure/flourish) |
| `platinum` | int | Platinum balance |

## Error Responses

```json
{ "error": "Character does not belong to this user" }
```

(Standard auth errors only.)

## Notes

- Ownership and loadout are **character-bound** (`rts_design.md` §15.3).
- Cosmetics are appearance-only and have no gameplay effect.
