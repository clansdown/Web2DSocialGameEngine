# `/api/settlePlatinum`

Settles a pending Platinum purchase, granting the Platinum exactly once. This is
the **mock stand-in for the payment provider's webhook** (`rts_design.md`
§15.1) — in production the provider calls the webhook server-side, and this
endpoint would be internal-only. It is exposed now so the purchase flow is
end-to-end testable.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "idempotency_key": "a1b2c3-...-uuid",
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
        "result": "settled",
        "granted": 500,
        "platinum": 500
    }
}
```

| Field | Type | Description |
|---|---|---|
| `result` | string | `settled`, `already_settled`, or `not_found` |
| `granted` | int | Platinum granted by this settlement |
| `platinum` | int | Balance after settlement |

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "idempotency_key required" }
```

## Notes

- Settling is idempotent: a replayed webhook returns `already_settled` and grants
  nothing further.
- A transaction can only be settled by the character that owns it.
- Platinum is **non-fungible** — it never converts to or from `gold`/`silver_pence`.
