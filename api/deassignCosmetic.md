# `/api/deassignCosmetic`

Clears a cosmetic from its loadout slot (the item stays owned and can be
re-equipped later).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "slot": "banner",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `character_id` | int | yes | The character whose loadout changes |
| `slot` | string | yes | `banner` \| `knight` \| `squad_uniform` \| `unit` \| `structure` \| `flourish` |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "loadout": { "knight": "knight_grey_plate" }
    }
}
```

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "unknown slot: hat" }
```

## Notes

- Deassigning never removes ownership; the item remains available.
- A refund (handled server-side) revokes ownership and clears the loadout
  (`rts_design.md` §15.4).
