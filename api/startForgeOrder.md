# `/api/startForgeOrder`

Starts a **forge order** at a tech-capable building (e.g. a blacksmith with the
recipe's `tech_node` learned). Pays the item's craft `materials` up front
(fungible money + auto-import, like building costs) and requires armory
headroom for the finished item. The order completes on the next economy tick
after `craft.duration_hours`, minting the item into `fiefdom_armory`.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "building_id": 3,
    "item_id": "sturdy_sword",
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "forge_order": { "item_id": "sturdy_sword", "start_ts": 1750006500, "duration_hours": 4 }
    }
}
```

## Error Responses

```json
{ "error": "Building does not belong to your fiefdom." }
{ "error": "This building already has an active forge order." }
{ "error": "The recipe for this item has not been learned." }
{ "error": "Your armory cannot store another forged item." }
{ "error": "You cannot afford the forge materials." }
{ "error": "This item cannot be forged at the manor." }
```

## Notes

- `max_concurrent_orders` per building (config, default 1) caps simultaneous
  forge orders; a second smith is how you forge in parallel.
- Forging does **not** block the smith's normal production.
- Armory **capacity** is slot-based (`armory_capacity` in `retinue.json`,
  `armory_slots` per item); player-initiated adds (forge/city buy) are blocked
  at capacity while grants/rewards always land over-cap.
- Forge completion is server-timed (economy tick), not client-timed.