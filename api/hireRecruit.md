# `/api/hireRecruit`

Hires a **named candidate** from the recruit market into the player's retinue.
The offer is validated **structurally**: a known class unlocked at the manor's
level, an in-range level, the **current or previous hour bucket** (grace
window), a name from the offer's language pool (English fallback), and an
existing headshot for the sex. The manor must also have capacity and the fee
must be affordable from fiefdom stores.

Name/headshot uniqueness is a **generation-time** property (the market re-rolls
past identities already in the roster), not a server gate: a scripted client may
hire a duplicate name, which is harmless because class and level — and therefore
the fee and stats — are still fully validated.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "unit_class": "spearman",
    "display_name": "William",
    "name_language": "en",
    "gender": "male",
    "headshot_id": "male/07.png",
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
            "headshot_id": "male/07.png",
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
{ "error": "You cannot afford this candidate's fee (gold + materials)." }
{ "error": "You need a manor before you can recruit a household." }
{ "error": "Character does not belong to this user" }
```

## Notes

- **Validation order**: structural offer validity (class/level/bucket/pools) →
  capacity → affordability. The fee is paid from the fiefdom on success through the same
  fungible path buildings use — **gold and silver_pence are one wallet**
  (1 gold = 240 pence per `economy.json` `currency`), a fee denominated in one
  can be paid from the other, and any **physical material shortfall is
  auto-imported with money** at the resource's import price (respecting the
  per-resource `import_settings` toggles).
- The knight (`is_knight = true`) can never be hired or dismissed; new
  **priority** ranks below every current member (see `/api/getRetinue`).
- `name_language` identifies the name pool that produced the offer. New clients
  echo it from `/api/listRecruitCandidates`; if omitted or invalid, the server
  falls back to the account's current language and then English. This keeps an
  offer hirable if the player changes language between listing and hiring.
- `headshot_id` persists on the member and is returned with the portrait in
  `/api/getRetinue`. Duplicate names/headshots are not rejected (generation
  avoids them in normal play).