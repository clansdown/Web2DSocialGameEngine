# `/api/joinBarony`

Joins an existing barony. A land-patent holder or a freeholder may join; the
character's existing fiefdom is reused (one fiefdom per character), and the
player transitions to the `sandbox` phase.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "barony_id": 2,
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
        "barony_id": 2,
        "fiefdom_id": 5,
        "game_phase": "sandbox",
        "base_unlocked": true
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "barony_id required" }
{ "error": "Character does not belong to this user" }
{ "error": "Barony not found" }
{ "error": "Already a member of a barony" }
{ "error": "Must have a land patent to join a barony" }
{ "error": "You must wait before joining another barony" }
```

The cooldown rejection also carries the expiry timestamp:

```json
{
    "error": "You must wait before joining another barony",
    "data": { "cooldown_until": 1700259200 }
}
```

## Notes

- Character must be in `land_patent`, `freehold`, or `freehold_qualified`; other
  phases are rejected with `Must have a land patent to join a barony`.
- The fiefdom is **created only if the character has none**; a freeholder's
  existing manor is reused, so tenure changes never destroy or duplicate it
  (one fiefdom per character). New fiefdoms start at `manor_level = 0`
  (home_base under construction) with the `economy.json.starting_resources`
  balance.
- Character can only be in one barony at a time; defunct (tombstoned) baronies
  are rejected with `Barony not found` and excluded from `/api/getBaronies`.
- After leaving a barony, a join cooldown applies
  (`progression.json` `barony.join_cooldown_days`, default 3 days). Until it
  expires the request fails with `You must wait before joining another barony`
  and a `cooldown_until` timestamp (`/api/getPlayerState` reports the same
  value as `barony_join_cooldown_until`).
- Fiefdom position is auto-assigned.
