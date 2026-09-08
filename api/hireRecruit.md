# `/api/hireRecruit`

Hires a **named candidate** from the recruit market into the player's retinue.
The offer must be verifiable — it has to match an offer actually generated for
this character in the **current or previous hour bucket** (re-derived from the
`(character_id, hour bucket)` seed, never stored) — and the manor must have
capacity, the name must be unused in the roster, and the fee must be
affordable from fiefdom stores.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "unit_class": "spearman",
    "display_name": "William",
    "gender": "male",
    "level": 2,
    "hour_bucket": 242365,
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

All offer fields come from the candidate object returned by
`/api/listRecruitCandidates`.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "member": {
            "id": 12,
            "character_id": 1,
            "display_name": "William",
            "unit_class": "spearman",
            "is_knight": false,
            "level": 2,
            "gender": "male",
            "health": 100,
            "health_updated": 1750006500,
            "priority": 1,
            "weapons": {},
            "armor": {},
            "equipment": {},
            "abilities": [],
            "status": "active",
            "created_at": 1750006500
        },
        "fee": { "gold": 75, "ironwork": 15 },
        "capacity": { "total": 3, "recruited": 2, "available": 1 }
    }
}
```

## Error Responses

```json
{ "error": "That offer has expired or was never made." }
{ "error": "Your manor cannot house anyone else in your retinue." }
{ "error": "Someone with that name already serves you." }
{ "error": "You cannot afford this candidate's fee (gold + materials)." }
{ "error": "You need a manor before you can recruit a household." }
{ "error": "Character does not belong to this user" }
```

## Notes

- **Validation order**: offer verifiability → capacity → name uniqueness →
  affordability. The fee is paid from the fiefdom on success through the same
  fungible path buildings use — **gold and silver_pence are one wallet**
  (1 gold = 240 pence per `economy.json` `currency`), a fee denominated in one
  can be paid from the other, and any **physical material shortfall is
  auto-imported with money** at the resource's import price (respecting the
  per-resource `import_settings` toggles).
- The knight (`is_knight = true`) can never be hired or dismissed; new
  **priority** ranks below every current member (see `/api/getRetinue`).
- Names must not duplicate an existing roster member. The market itself may
  still offer a name already in the roster — hire simply rejects it.