# `/api/foundFreehold`

Founds an **Outlying Freehold** — a lone knight's manor unattached to any
barony. Transitions the player from `land_patent` to `freehold` and unlocks the
manor. Only a land-patent holder may found a freehold.

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
        "fiefdom_id": 5,
        "game_phase": "freehold",
        "base_unlocked": true
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "Character does not belong to this user" }
{ "error": "Must have a land patent to found a freehold" }
{ "error": "Already a member of a barony" }
```

## Notes

- Character must be in the `land_patent` game phase and must not already belong
  to a barony.
- Creates (or reuses) the character's fiefdom with `manor_level = 0` and the
  `economy.json.starting_resources` balance. A character owns **at most one
  fiefdom**; it persists across later tenure changes (joining a barony, later
  leaving, or applying for a mesne lordship all reuse the same manor).
- Transitions to the `freehold` phase with `base_unlocked = 1`. The hub then
  exposes the `manor` activity.
- Once the freeholder also meets the mesne-lord requirements (manor house level
  ≥ 6 and the configured RTS milestone, e.g. defeating an Ettin), the next
  `/api/getPlayerState` lazily promotes them to `freehold_qualified` and sends
  the king's letter. See `/api/applyForMesneLord`.
