# POST /api/setFiefdomReserve

Sets the reserve (minimum stock kept) for a resource in a fiefdom. Each economy tick,
any amount of a resource above its reserve is auto-sold at the resource's export price:
`export_prices[resource]` (explicit), else `export_sell_multipliers[resource]` × import
price, else the global `export_sell_multiplier` (default 0.5) × import price. Sales credit
`silver_pence` for penny-market resources like grain (money-form import price) and gold otherwise.
Amounts at or below the reserve are kept as stockpiles.

## Request

```json
{
  "fiefdom_id": 1,
  "resource": "grain",
  "reserve": 100
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| fiefdom_id | integer | Yes | Fiefdom to update |
| resource | string | Yes | Resource name (e.g. "grain", "wood", "ironwork") |
| reserve | number | Yes | Minimum amount to keep in stock. 0 = sell all excess. Negative values are clamped to 0. |

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "reserves": {
      "grain": 100,
      "wood": 50
    }
  }
}
```

`reserves` returns the stored overrides (defaults are merged server-side in `/api/getFiefdom`).

### Error (400 Bad Request)

```json
{
  "error": "fiefdom_id, resource, and reserve required"
}
```

## Notes

- Requires authentication.
- Stored per fiefdom in the `reserves` TEXT JSON column (default `{}`).
- Effective reserve for a resource = stored override if present, else
  `economy.json.default_reserves[resource]`, else 0.
- Selling happens after production, consumption, and imports, before gold consumption.
- `gold` and `peasants` are never sold.
