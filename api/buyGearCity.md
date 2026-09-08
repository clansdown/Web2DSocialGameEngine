# `/api/buyGearCity`

Purchases a gear item from **"the city"** at its config `city_price` — an
extreme markup bypass for lords who skipped their own blacksmith. No tech tree
needed; gold paid through the fungible money path; the item lands in the
**armory** (player-initiated add: blocked when the armory is at capacity).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "item_id": "war_horse",
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": { "item_id": "war_horse", "armory_id": 12, "gold": 250 }
}
```

## Error Responses

```json
{ "error": "Unknown equipment item." }
{ "error": "This item is not sold in the city." }
{ "error": "Your armory cannot store another purchased item." }
{ "error": "You cannot afford this item in the city." }
```

## Notes

- `city_price` is deliberately **extremely expensive** relative to forging —
  a gold sink and a way for a poor lord to be kept honest by their wallet.
- Item requires armory headroom (`armory_slots` vs `retinue.json
  armory_capacity`); grants/rewards still land over-cap.