# `/api/equipCosmetic`

Equips an owned cosmetic into its loadout slot. The server validates that the
slot is one of the six cosmetic slots and that the cosmetic is owned and
**fits the slot** (a banner cannot be equipped as a knight, etc.).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "slot": "banner",
    "cosmetic_id": "banner_emberfield",
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
| `cosmetic_id` | string | yes | An owned cosmetic whose `slot` matches |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "loadout": { "banner": "banner_emberfield" }
    }
}
```

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "unknown slot: hat" }
{ "error": "cosmetic_id required" }
{ "error": "unknown cosmetic: foo" }
{ "error": "cosmetic does not fit slot knight" }
{ "error": "cosmetic not owned" }
```

## Notes

- Cosmetics ride the combat join-time snapshot; equipping takes effect on the
  next match (`rts_design.md` §15.3).
- Appearance only — no gameplay effect (§15.5.1).
