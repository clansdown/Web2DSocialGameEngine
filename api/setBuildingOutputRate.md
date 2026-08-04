# `/api/setBuildingOutputRate`

Sets how much of a building's maximum possible output actually runs (0..1),
for a single output of a building that uses the multi-output `outputs` schema.
Scaling the rate also scales that output's input requirements; a rate of 0
disables the output entirely (no inputs consumed, no production).

**Requires authentication.**

## Request

```json
{
    "building_id": 5,
    "output": "fancy_ironwork",
    "rate": 0.0,
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| building_id | integer | Yes | Building instance ID |
| output | string | Yes | Output resource name (e.g. "ironwork", "fancy_ironwork") |
| rate | number | Yes | Utilization 0..1 (0 = off, 1 = full). Clamped to the range. |

## Success Response

```json
{
    "status": "ok",
    "data": {
        "output_rates": {
            "ironwork": 1.0,
            "fancy_ironwork": 0.0
        }
    }
}
```

`output_rates` returns the building's full updated rates map (entries absent for
outputs left at their default of 1.0).

## Error Responses

```json
{ "error": "building_id required" }
{ "error": "output required" }
{ "error": "rate required" }
{ "error": "Building not found" }
{ "error": "Character does not belong to this user" }
{ "error": "That building does not produce fancy_ironwork" }
{ "error": "That output is not unlocked until level 2" }
```

## Notes

- Requires authentication; the character must own the fiefdom that owns the building.
- The output must be produced by the building type's config (`outputs` array or
  a flat production field), and must be unlocked at the building's level
  (`building.level >= min_level`).
- Rates are stored per building instance in `fiefdom_buildings.output_rates`
  (JSON); missing entries default to 1.0, so a newly unlocked output (e.g.
  `fancy_ironwork` at blacksmith level 2) starts on.
- Read back via `/api/getFiefdom` (`include_buildings=true`): each building
  object carries its `output_rates`.
