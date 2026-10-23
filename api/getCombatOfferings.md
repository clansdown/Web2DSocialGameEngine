# `/api/getCombatOfferings`

Returns the character's **current-hour** auto-generated PvE mission offerings,
grouped by tier (`rts_design.md` §3.1). The board refreshes every hour; an
offering completed by a won mission is excluded. See `/api/combatCreate` for
creating a match from an offering.

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

| Field | Type | Required | Description |
|---|---|---|---|
| `character_id` | int | yes | The character whose board to return |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "tiers": [
            { "id": "solo", "min_players": 1, "max_players": 1 },
            { "id": "duo", "min_players": 2, "max_players": 2 },
            { "id": "company", "min_players": 3, "max_players": 5 }
        ],
        "offerings": [
            {
                "id": "1.490000.solo.0.1.1.481",
                "tier": "solo",
                "index": 0,
                "ruleset": "skirmish",
                "map_id": "meadow",
                "xp_base": 54.0,
                "danger_band": "easy",
                "danger_ratio": 0.72,
                "encounter_power": 3200.0,
                "target_power": 3200.0,
                "realized_power": 3080.0,
                "difficulty": 0.72,
                "eliteness": -0.31,
                "warband": [
                    { "unit_class": "goblin", "level": 2, "count": 3, "group": "goblin", "tactic": "charge" },
                    { "unit_class": "bandit", "level": 1, "count": 1, "group": "bandit", "tactic": "skirmish" }
                ],
                "total_units": 4
            }
        ]
    }
}
```

| Field | Type | Description |
|---|---|---|
| `tiers` | array | The configured tiers (`game/config/combat/offerings.json`). `duo`/`company` are only offered when the character is in a barony |
| `offerings` | array | The current-hour board for every applicable tier, minus completed offerings |
| `offerings[].id` | string | The offering id (`<player_id>.<hour_bucket>.<tier>.<index>.<players>.<troop_count>.<power_bucket>`); pass back to `combatCreate` |
| `offerings[].tier` | string | Tier id (`solo` / `duo` / `company`) |
| `offerings[].map_id` | string | The map chosen for this offering |
| `offerings[].xp_base` | number | Strength-derived XP preview (`xp.per_power` × the warband's **realized** aggregate strength) |
| `offerings[].danger_band` | string | `trivial` / `easy` / `fair` / `hard` / `deadly` |
| `offerings[].danger_ratio` | number | Realized warband power ÷ the party's frozen aggregate power (`S`) |
| `offerings[].encounter_power` | number | Target power `T = ρ·S` (preview alias, kept for the client) |
| `offerings[].target_power` | number | The same target `T` |
| `offerings[].realized_power` | number | The assembled warband's actual aggregate power `R` |
| `offerings[].difficulty` | number | The drawn relative difficulty `ρ` (`difficulty.band_min…band_max`) |
| `offerings[].eliteness` | number | The drawn horde/elite coefficient `e` (`< 0` horde, `> 0` elite) |
| `offerings[].warband` | array | Composition preview (`unit_class` / `level` / `count` / `group` / `tactic` / `aggro_model`) |
| `offerings[].total_units` | int | Total foes |

## Error Responses

```json
{ "error": "Character does not belong to this user" }
```

## Notes

- Offerings are **deterministic and never stored**: the id encodes the seed
  `(player_id, hour_bucket, tier, index, players, troop_count, power_bucket)` and
  the content is a pure function of the id. Only completions and attempt logs
  are persisted. A party that grows keeps older offers valid; one that shrinks
  below the frozen size voids them.
- The board is **per character** — a barony member sees their own board; co-op
  happens by sharing a match code (the warband is fixed when the lobby opens).
- Offerings remain valid (joinable by code) for `board.validity_hours` after
  their hour, so an invite does not need to be accepted immediately; past that
  the warband has "moved on".
