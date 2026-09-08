# `/api/getRetinueGear`

Returns the fiefdom's **armory** (every gear item in storage, un-equipped `NULL`
or assigned to a member) and **general storage** (count-based items such as
books). Item configs are merged in so the client can render names/slots/costs.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "armory": [
            {
                "id": 7,
                "item_id": "sturdy_sword",
                "member_id": null,
                "created_at": 1750006500,
                "item": {
                    "name": "Sturdy Sword",
                    "slot": "weapon",
                    "requires_level": 1,
                    "upkeep": { "ironwork": 0.5 },
                    "armory_slots": 1,
                    "base_value": 20,
                    "city_price": 60,
                    "craft": { "duration_hours": 4, "materials": { "ironwork": 5 }, "tech_node": "swordsmithing" }
                },
                "sell_value": 5
            }
        ],
        "storage": [
            { "item_id": "treatise_on_smithing", "count": 1, "item": { "name": "Treatise on Smithing", "source": "drop", "xp_grant": 25, "training_duration_hours": 2 } }
        ]
    }
}
```

## Error Responses

```json
{ "error": "Character does not belong to this user" }
```

## Notes

- **Armory** is per-instance gear (`fiefdom_armory`); `member_id NULL` = in
  storage. Equipped members' gear shows here too (`member_id` set) and in the
  member's `equipment` slot map.
- **General storage** is count-based (`fiefdom_storage`) and deliberately
  generic — books today, potions/reagents later.
- `sell_value` = `base_value × sell_discount` (`retinue.json`), the at-discount
  liquidation price `/api/sellItem` credits.