# `/api/getRetinue`

Returns the character's full **retinue** — the knight (the character itself)
plus every unit they have mustered — along with the manor's **retinue capacity**
(the knight is always "free"). Members are created via the recruit market
(`/api/listRecruitCandidates` + `/api/hireRecruit`) or the manor game's
`train_troops` action (stub). The knight row is auto-created on first access.
Combat matches snapshot the *active* members when a player joins (see
`docs/combat_protocol.md` for the snapshot shape in `welcome`).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
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
        "members": [
            {
                "id": 1,
                "character_id": 1,
                "display_name": "Sir Wolf",
                "unit_class": "knight",
                "is_knight": true,
                "level": 1,
                "gender": "male",
                "health": 100,
                "health_updated": 1750000000,
                "priority": 0,
                "weapons": {},
                "armor": {},
                "equipment": {},
                "abilities": [],
                "status": "active",
                "maintained": true,
                "fieldable": true,
                "created_at": 1750000000
            }
        ],
        "capacity": {
            "manor_level": 1,
            "total": 2,
            "recruited": 0,
            "available": 2
        },
        "recovery": {
            "max_recovery_hours": 16,
            "min_deploy_hp": 50,
            "multiplier": 1
        }
    }
}
```

## Error Responses

```json
{ "error": "Character does not belong to this user" }
```

## Notes

- **Member fields**
  - `gender` — `male` | `female`; drives the recruit name pool and (future)
    gender-aware prose.
  - `health` / `health_updated` — persistent health 0..100 with the last
    snapshot timestamp. Health recovers over wall-clock time (a full heal takes
    up to `recovery.max_recovery_hours`, faster with an infirmary
    `recovery_multiplier`); a member below `recovery.min_deploy_hp` has
    `fieldable: false` and is locked out of combat until healed past it.
  - `fieldable` — `health >= recovery.min_deploy_hp` (deploy at current HP above
    that; below it the member is "in the infirmary").
  - `priority` — strict intra-roster rank (funding + infirmary beds; player
    reorderable via `/api/setRetinuePriority`, which re-ranks from a submitted
    permutation of the non-knight member ids). New members rank below everyone.
  - `maintained` — whether the economy tick fully funded this member in the
    current tick (priority order, all-or-nothing per member; shortfalls auto-
    imported with fungible money). Unfunded members incur the configured
    general-morale penalty and their class's unfunded effect (Phase-4+ combat).
  - `equipment` — generic slot→item map (`weapon`, `armor`, `mount`,
    `potions`, ...). `weapons`/`armor` are legacy columns kept for back-compat.
- **Capacity** — `total` is `retinue_capacity_by_level[manor_level]` from
  `retinue.json` and counts *recruited* members only; the knight is free.
- `status` is `active` | `casualty` | `retired`. In the new no-death model the
  worst combat outcome is infirmary time (`health` < threshold); `casualty`
  remains only for legacy/combat-persistence behavior until integrated.
- Schema documented in `server/tables/retinue_members.md`.