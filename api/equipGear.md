# `/api/equipGear`

Equips an **armory item** to a retinue member. Validates: the item is the
player's and un-equipped, the member belongs to the player, the member's level
meets `requires_level`, and the member's class passes `allowed_classes` (when
present). The item is stored in the member's `equipment` slot map
(`equipment[slot] = item_id`) and tracked on the armory row.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "armory_id": 7,
    "member_id": 12,
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": { "member_id": 12, "slot": "weapon", "item_id": "sturdy_sword" }
}
```

## Error Responses

```json
{ "error": "Armory item not found." }
{ "error": "That item is already equipped." }
{ "error": "Member does not belong to you." }
{ "error": "This item requires a higher member level." }
{ "error": "This item cannot be used by that unit class." }
{ "error": "Unknown item." }
```

## Notes

- Gear slots are config-driven (`equipment.json` `slots`): weapon, armor, mount,
  potions. The basic kit fills any empty slot implicitly (class-inherent,
  invisible, zero upkeep) — equipping replaces it.
- Equipped gear is the future equipment-upkeep stream (Phase maintenance).