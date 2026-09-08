# `/api/listRecruitCandidates`

Lists the current **recruit market** for the player's manor: a roster of named
candidates a lord can hire into their retinue. The market is generated
deterministically from `(character_id, hour bucket)` — never stored — so the
same offers reappear identically within an hour (and on every request).

The current hour's cohort is returned **plus the previous hour's cohort**, which
remains hirable during a one-hour **grace window** (`market.grace_buckets`).
After that, an offer expires and can never be hired.

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
        "candidates": [
            {
                "unit_class": "spearman",
                "display_name": "William",
                "gender": "male",
                "level": 2,
                "hour_bucket": 242365,
                "expires_at": 1750010400,
                "fee": { "gold": 75, "ironwork": 15 }
            }
        ],
        "current_bucket": 242365,
        "next_refresh_at": 1750006800,
        "manor_level": 2,
        "capacity": { "total": 3, "recruited": 1, "available": 2 }
    }
}
```

## Error Responses

```json
{ "error": "You need a manor before you can recruit a household." }
{ "error": "Character does not belong to this user" }
```

## Notes

- **Candidate generation** — `market.candidates_per_class` (default 6)
  candidates per class unlocked at the manor's level (`requires_manor_level`
  per class in `player_combatants.json`). Candidate **level** is uniform in
  `1 .. max_candidate_level`, where `max_candidate_level = manor_level +
  market.max_candidate_level_offset` (clamped to the class `max_level`).
  Gender and name come from the per-class name pools
  `game/text/en/names_<class>_male.txt` / `_female.txt`.
- **`hour_bucket`** — `floor(now / 3600)`; the seed input for candidate
  generation. `next_refresh_at` is the unix ts when the pool advances (the
  current cohort becomes the grace cohort).
- **`expires_at`** — the last moment the offer is hirable. For the current
  cohort this is the end of the next hour (current hour + grace bucket); for
  the grace cohort it is the end of the current hour.
- **`fee`** — the per-resource hire fee at the candidate's level, from the
  class `costs` array (level-indexed, linearly extended beyond its last
  entry). Gold + materials are paid from fiefdom stores on hire.
- Hiring a candidate is `/api/hireRecruit`. The knight is free; capacity counts
  recruited members only.