# `/api/purchasePlatinum`

Starts a real-money purchase of a **Platinum** product (a Platinum pack, or a
directly-priced product such as the monthly pass). The payment gateway is a
**pluggable stub** (`rts_design.md` §15.1): this call records a **pending**
transaction and returns a payment intent; no Platinum is granted until the
transaction is settled (see `/api/settlePlatinum`).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "product_id": "platinum_pouch",
    "idempotency_key": "a1b2c3-...-uuid",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `character_id` | int | yes | The buying character |
| `product_id` | string | yes | A product id from `getStore` |
| `idempotency_key` | string | yes | Unique key; replays do not create a second pending transaction |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "result": "pending",
        "product_id": "platinum_pouch",
        "transaction_id": 12,
        "payment_intent": "mock://platinum/a1b2c3-...-uuid",
        "price_real": 4.99,
        "platinum": 0
    }
}
```

| Field | Type | Description |
|---|---|---|
| `result` | string | `pending` (new intent) or `duplicate` (key already used) |
| `payment_intent` | string | Gateway stub handle; a real provider's intent/URL replaces this |
| `platinum` | int | Balance (unchanged until settlement) |

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "product_id and idempotency_key required" }
{ "error": "unknown product: foo" }
```

## Notes

- **Settlement is deferred.** In production the provider's webhook settles the
  transaction server-side; `/api/settlePlatinum` is the mock stand-in.
- Platinum is **variety only** — it can never buy power (`rts_design.md` §15.0).
- No loot boxes: every product is a deterministic, directly-selected purchase.
