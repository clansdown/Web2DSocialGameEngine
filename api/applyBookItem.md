# `/api/applyBookItem`

Consumes **one unit** of a training item (a book/scroll) from the fiefdom's
general storage (`fiefdom_storage`) and starts a one-at-a-time **training
timer** on a tech-capable building. When the timer elapses the building gains
the item's `xp_grant` technology XP.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "building_id": 3,
    "item_id": "treatise_on_smithing",
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "training": { "grant_xp": 25, "start_ts": 1750006500, "duration_hours": 2 },
        "item_id": "treatise_on_smithing"
    }
}
```

## Error Responses

```json
{ "error": "Building does not belong to your fiefdom." }
{ "error": "This building has no technology tree." }
{ "error": "This building is already training." }
{ "error": "Unknown item." }
{ "error": "That item is not a training item." }
{ "error": "You do not have that item." }
```

## Notes

- Item data from `items.json` (`xp_grant`, `training_duration_hours`,
  `source: "drop"`); books arrive as drops (campaign rewards to be wired).
- **One training (any kind) per building** — teachers and books share the same
  single slot per the no-stacking rule.
- Books can also be **sold** from storage (`/api/sellItem` kind storage) — they
  are rewards, but a surplus one can be liquidated.