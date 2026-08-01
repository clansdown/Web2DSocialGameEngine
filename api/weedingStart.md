# `/api/weedingStart`

Starts a weeding (assarting) session. During the campaign (`level_id > 0`) the
level config determines difficulty and allowed plants/tools. In ongoing mode
(`level_id: 0`) the difficulty and grid size are chosen by the player and
validated against the server's `weeding/ongoing.json` config.

**Authentication:** Required

## Request

### Campaign level

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "character_id": 1,
  "level_id": 3
}
```

### Ongoing mode

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "character_id": 1,
  "level_id": 0,
  "difficulty": 2,
  "grid_size": 4
}
```

- `difficulty`: one of `difficulty_options` in `game/config/weeding/ongoing.json` (default 1)
- `grid_size`: one of `size_options` in the same config (default: config default)

If `difficulty` or `grid_size` is not offered by the config, the server rejects
the request (`"Invalid difficulty or grid size for ongoing weeding"`).

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "session_id": 42,
    "character_id": 1,
    "level_id": 0,
    "board": [],
    "grid_size": 4,
    "round": 1,
    "actions_remaining": 2,
    "equipped_tool": "sickle",
    "par": 9,
    "won": false,
    "score": 0,
    "available_tools": [],
    "available_plants": [],
    "available_specials": [],
    "map_metadata": {},
    "message": "Weeding session started"
  },
  "token": "new-token-string"
}
```

### Error

```json
{ "status": "ok", "error": "Invalid difficulty or grid size for ongoing weeding" }
```

### Notes

- The chosen `difficulty` and `grid_size` are stored in the session and used to
  compute the silver reward (via the shared reward pool) when the session is won.
- A session can be resumed with `weedingStart` while one is active.
