# `/api/applyForMesneLord`

Applies to become a **Mesne Lord**: a qualified freeholder founds an **Honor**
(a barony under its pre-elevation name). Reuses the freeholder's existing manor
and enters the same `sandbox` state as the baron-track founders.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "name": "Stormhold",
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
        "barony_id": 3,
        "fiefdom_id": 5,
        "game_phase": "sandbox",
        "base_unlocked": true
    }
}
```

## Error Responses

```json
{ "error": "character_id required" }
{ "error": "name required" }
{ "error": "name must be 64 characters or fewer" }
{ "error": "You are not eligible to become a mesne lord" }
{ "error": "Already a member of a barony" }
{ "error": "A barony with that name already exists" }
```

## Notes

- Character must be in the `freehold_qualified` phase and not already a barony
  member. The server **re-checks** the requirements (defense in depth) — the
  configured manor-house level (`progression.json` `mesne_lord.requirements.manor_level`,
  default 6) and every listed RTS milestone (e.g. `ettin_slain`).
- `name` is required, trimmed, capped at 64 characters, and must be unique
  **case-insensitively** against `baronies.name`. Tombstoned (defunct) barony
  names still count as taken.
- Creates a `baronies` row plus a `mesne_lord` membership on the character's
  **existing** fiefdom (a character owns at most one fiefdom), syncs the barony
  chat, writes `player_game_state.honor_name`, and transitions to `sandbox`.
- A qualified freeholder reaches this phase via the lazy promotion in
  `/api/getPlayerState`, which also sends the one-time king's letter
  (`letter_mesne_lord_eligible`). Letters are read via `/api/getLetters`.
