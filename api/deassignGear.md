# `/api/deassignGear`

Returns a member's equipped slot back to the **armory** (member's `equipment`
slot map loses the item; the empty slot reverts to the class **basic kit**).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "member_id": 12,
    "slot": "weapon",
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
{ "error": "Member does not belong to you." }
{ "error": "That slot is empty." }
```

## Notes

- The item is handed back to the armory with `member_id = NULL`, ready to be
  equipped to another member or sold.
- Selling requires de-equipping first (`/api/sellItem` rejects equipped gear).