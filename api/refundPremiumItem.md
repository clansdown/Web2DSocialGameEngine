# `/api/refundPremiumItem`

Refunds a cosmetic the character owns. Per `rts_design.md` §15.4/§15.8, a refund
**revokes** rather than rolling back: the ownership row is flagged `revoked`
(visually locked) and any loadout slot using the item is cleared. No Platinum is
credited. The call is server-authoritative and idempotent — refunding an item the
character does not own reports `not_owned` and changes nothing.

**Requires authentication.** This is a support/administrative action, not a
normal player store button.

## Request

```json
{
    "character_id": 1,
    "item_id": "banner_emberfield",
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
        "result": "revoked",
        "owned": ["banner_ravensworn"],
        "loadout": { "knight": "knight_grey_plate" }
    }
}
```

| Field | Type | Description |
|---|---|---|
| `result` | string | `revoked` (was owned) or `not_owned` |
| `owned` | string[] | Non-revoked cosmetic ids after the refund |
| `loadout` | object | Loadout after clearing any slot that used the item |

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "item_id required" }
{ "error": "unknown item: foo" }
```

## Notes

- Revoked items can be re-granted/re-purchased later (the flag flips back).
- No Platinum is returned — Platinum and cosmetics are non-tradable, and refunds
  are a content-integrity tool (`rts_design.md` §15.5.3).
