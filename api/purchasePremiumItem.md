# `/api/purchasePremiumItem`

Buys a cosmetic (`item_id`) or a bundled cosmetic pack (`bundle_id`) with the
character's **Platinum**. The purchase is **idempotent** and
server-authoritative: the server checks balance, ownership, and the idempotency
key, debits Platinum once, grants ownership, and records a settled transaction.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "item_id": "banner_emberfield",
    "idempotency_key": "6c2f0b1e-...-uuid",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `character_id` | int | yes | The buying character |
| `idempotency_key` | string | yes | Client-generated unique key; a replay yields exactly one grant |
| `item_id` | string | one of | A cosmetic id from `getStore` |
| `bundle_id` | string | one of | A bundle id from `getStore` |

Provide exactly one of `item_id` / `bundle_id`.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "result": "ok",
        "platinum": 50,
        "owned": ["banner_emberfield", "banner_ravensworn"]
    }
}
```

`result` is one of:

| Value | Meaning | Charged? |
|---|---|---|
| `ok` | Purchased | yes |
| `already_owned` | Already owned (idempotent no-op) | no |
| `duplicate` | This `idempotency_key` was already used | no |

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "idempotency_key required" }
{ "error": "item_id or bundle_id required" }
{ "error": "unknown item: foo" }
{ "error": "unknown bundle: bar" }
{ "error": "insufficient platinum" }
```

## Notes

- The same `idempotency_key` never charges twice; an already-owned item never
  charges again.
- Platinum is a separate ledger, never usable for the normal economy.
- Cosmetics carry no gameplay effect by construction (`rts_design.md` §15.5.1).
