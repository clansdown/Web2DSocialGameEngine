# `/api/getTechTrees`

Returns every technology tree from `tech_trees.json` plus the per-building
progression state of the character's tech-capable buildings (each placed
building that declares `tech_trees` in its config): current XP, learned nodes,
and any active forge order / training timer.

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "trees": {
            "smithing": {
                "name": "Smithing",
                "nodes": [
                    { "id": "swordsmithing", "name": "Swordsmithing", "xp_cost": 40,
                      "prerequisites": [],
                      "effects": [{ "type": "unlock_recipe", "recipe": "sturdy_sword" }] }
                ]
            }
        },
        "buildings": [
            {
                "id": 3,
                "name": "blacksmith",
                "level": 2,
                "tech_trees": ["smithing"],
                "tech_xp": 12,
                "tech_nodes": ["swordsmithing"],
                "forge_order": null,
                "training": null
            }
        ]
    }
}
```

## Error Responses

```json
{ "error": "Character does not belong to this user" }
```

## Notes

- `tech_nodes` names the learned node ids (spending XP via
  `/api/learnTechNode`). `forge_order`/`training` are the active timer JSON
  (`{item_id|grant_xp, start_ts, duration_hours}`) or `null`.
- XP accrues passively in the economy tick at the building's
  `tech_xp_per_day[level-1]` per day.