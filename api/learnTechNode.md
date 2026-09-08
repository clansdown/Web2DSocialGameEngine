# `/api/learnTechNode`

Spends a building's technology XP to **learn a node** in one of its declared
trees (`tech_trees`). The node's prerequisites must already be learned and the
building must have enough `tech_xp`. Persisted in `fiefdom_buildings.tech_nodes`
(+ `tech_xp` reduced by the node cost).

**Requires authentication.**

## Request

```json
{
    "character_id": 1,
    "building_id": 3,
    "node_id": "arming_kit",
    "auth": { "username": "player", "token": "session_token_hex" }
}
```

## Success Response

```json
{
    "status": "ok",
    "data": {
        "node_id": "arming_kit",
        "tech_xp": 0,
        "tech_nodes": ["arming_kit"]
    }
}
```

## Error Responses

```json
{ "error": "Building does not belong to your fiefdom." }
{ "error": "Not enough technology XP for this node." }
{ "error": "Prerequisite nodes not learned." }
{ "error": "Node already learned." }
{ "error": "This building has no technology tree." }
{ "error": "Unknown technology node for this building." }
```

## Notes

- Node effects are the node's `unlock_recipe` entries — learning the node makes
  that item forgeable at this building (`/api/startForgeOrder`).
- Two blacksmiths diverge: each instance keeps its own XP + learned nodes, so
  the same tree supports weaponsmith vs armorer specializations.