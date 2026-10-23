# `/api/leaveBarony`

Leaves the character's barony. A non-founder leaves freely; a founder must name
another member as the new baron, unless they are the last member — in which case
the barony is tombstoned. The leaver keeps their fiefdom and becomes a
freeholder, and a join cooldown is stamped.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "successor_character_id": 7,
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

`successor_character_id` is required only when the caller is the founder and the
barony has more than one member.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "game_phase": "freehold",
        "left_barony_id": 2
    }
}
```

`game_phase` is `freehold_qualified` instead when the leaver already meets the
mesne-lord requirements (manor house level ≥ 6 and the configured milestone).

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "Character does not belong to this user" }
{ "error": "Not a member of a barony" }
{ "error": "successor_character_id required to leave as founder" }
{ "error": "successor must be another member of the barony" }
```

## Notes

- **Founder leaving with other members:** `successor_character_id` must name
  another member; ownership (`baronies.owner_character_id`) and leadership
  (`baronies.baron_character_id`) are reassigned to that successor, whose
  `barony_members.role` becomes `baron`.
- **Solo founder:** the barony is **tombstoned** (`baronies.defunct_at = now`).
  It is hidden from `/api/getBaronies` and its chat is archived, but the name
  stays reserved (case-insensitive) and cannot be reused.
- The leaver's fiefdom is kept (never destroyed) and the phase becomes
  `freehold` (or `freehold_qualified` when the qualifications are already met).
- A join cooldown is stamped in `player_game_state.barony_left_at` (3 days by
  default, from `progression.json` `barony.join_cooldown_days`). While the
  cooldown is active `/api/joinBarony` returns
  `"You must wait before joining another barony"` with a `cooldown_until`
  timestamp. `/api/getPlayerState` reports it as `barony_join_cooldown_until`.
