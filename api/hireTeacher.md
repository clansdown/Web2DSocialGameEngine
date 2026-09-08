# `/api/hireTeacher`

Hires a **teacher** — an on-demand, non-stored service that starts a
one-at-a-time **training timer** on a tech-capable building. The teacher's gold
fee is paid (fungible path); when the timer elapses the building gains
`teacher_xp_grant` technology XP (the economy tick lands it).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "building_id": 3,
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "training": { "grant_xp": 50, "start_ts": 1750006500, "duration_hours": 4 }
    }
}
```

## Error Responses

```json
{ "error": "Building does not belong to your fiefdom." }
{ "error": "This building has no technology tree." }
{ "error": "This building is already training." }
{ "error": "You cannot afford a teacher." }
```

## Notes

- Values come from `retinue.json` `training` (`teacher_gold_cost` /
  `teacher_xp_grant` / `teacher_duration_hours`).
- **One training (any kind) per building** — a book applied while a teacher is
  tutoring is rejected. Demolishing the building mid-training loses the grant
  (a consumable is consumed regardless).
- Teachers have no inventory; they are hired at will (unlike books, which drop).