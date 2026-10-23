# `/api/combatCreate`

Creates a new realtime combat match lobby. The creating character becomes the
host and first player; the returned **match code** is shared with invited
players (e.g. barony members), who join via `/api/combatJoin`. The battle
itself runs over the WebSocket route `/ws/combat` (see `docs/combat_protocol.md`).

A mission is created from an **opportunity** (`rts_design.md` §3.1): the ruleset
+ map + **warband** are fixed at creation (Decision 62) — joining or leaving
never changes the fight.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "mode": "pve",
    "offering_id": "1.490000.solo.3.481.481",
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `character_id` | int | yes | The host character |
| `mode` | string | yes | `"pve"` only — PvP matchmaking is not implemented yet |
| `offering_id` | string | yes* | An auto-generated offering id from `getCombatOfferings`. The id encodes the seed, so the server re-derives the offering and validates it (authenticity + freshness) |
| `opportunity_id` | string | no | An authored opportunity id from `combatGetConfigs` (`combat/warbands.json`); carries ruleset + map + the fixed warband |
| `ruleset_id` | string | no | Legacy path — a ruleset id from `combat/rulesets.json` (used when no offering/opportunity is given) |
| `map_id` | string | no | Legacy path — a map name from `combat/maps/` (filename without `.json`) |

\* One of `offering_id`, `opportunity_id`, or both `ruleset_id`+`map_id` must be
provided. The generated-offering route is canonical; authored opportunities are
the hand-crafted specials.

## Success Response

```json
{
    "status": "ok",
    "data": {
        "match_id": "m42",
        "match_code": "AB3K9X",
        "mode": "pve",
        "ruleset_id": "skirmish",
        "map_id": "meadow",
        "offering_id": "1.490000.solo.3.481.481",
        "tier": "solo",
        "danger_band": "fair"
    }
}
```

(For an authored opportunity the `opportunity_id` + numeric `tier` fields are
returned instead.)

## Error Responses

```json
{ "error": "Character does not belong to this user" }
{ "error": "Only pve matches are available yet — pvp matchmaking is not implemented" }
{ "error": "invalid offering id" }
{ "error": "offering already completed" }
{ "error": "offering is no longer valid" }
{ "error": "unknown opportunity: foo" }
{ "error": "ruleset_id required" }
{ "error": "unknown ruleset: foo" }
{ "error": "unknown map: bar" }
```

## Notes

- **Generated offerings** are one-shot: a won mission is recorded in
  `character_mission_completions` (per participant) and no longer offered. A
  defeat/draw/forfeit leaves the offering available to retry. The
  **authenticity** check (`mission_offerings::validate_offering_authentic`)
  verifies the player id, the validity window (`board.validity_hours`), and that
  the offering's frozen party fields (players / troop count / power bucket) are
  at or below the character's current party. Once a match is created the
  decision to start is trusted, so the result is honoured even if power changes
  afterwards.
- Every finished attempt is logged in `combat_mission_attempts` (with the
  warband's end state), and per-class enemy deaths increment `character_counters`
  (`slain.<class>`).

- The player's active retinue (`retinue_members` where `status = 'active'`,
  health ≥ `min_deploy_hp`, knight first) is snapshotted into the match at
  creation time. Roster select happens over the WebSocket (`roster` message)
  before the deploy phase.
- The match lives **entirely in RAM** — nothing is written to SQLite until a
  battle ends and casualties, **XP**, and morale are persisted.
- Rulesets, maps, and warbands are hot-reloadable — no server restart needed.