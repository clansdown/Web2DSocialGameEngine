# `/api/combatMatchmaking`

PvP matchmaking — **STUB**. The full system (challenges, acceptances, and
handicap matchups like 1 high-level player vs 5 low-level players) is a later
milestone; this endpoint always returns an error so clients can wire the UI
now and have it light up later.

**Requires authentication.**

## Request

```json
{
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

## Response

```json
{ "error": "PvP matchmaking is not yet implemented" }
```

## Notes

- TODO: challenges/acceptances flow, team size handicaps, skill-based pairing.
- PvP matches themselves are already supported by the engine (team assignment
  alternates teams; `combat/rulesets.json` has a `scrimmage` pvp ruleset) —
  only the queue/accept layer is missing.
