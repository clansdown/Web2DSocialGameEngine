# `/api/getPlayerState`

Retrieves the current player's game state including phase, active mini-game, level progress, and tenure (barony membership, letters, join cooldown). Also performs the lazy `freehold` → `freehold_qualified` promotion.

**Authentication:** Required

## Request

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "character_id": 1
}
```

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "character_id": 1,
    "game_phase": "initial_mission",
    "available_activities": ["tasks", "profile", "chat"],
    "current_mini_game": null,
    "current_level_id": null,
    "base_unlocked": false,
    "entered_at": 1700000000,
    "last_updated": 1700000000,
    "has_letters": false,
    "barony_join_cooldown_until": 0,
    "progress": [
      {
        "id": 1,
        "character_id": 1,
        "mini_game": "tower_defense",
        "level_id": 1,
        "completed": true,
        "best_score": 85,
        "times_played": 2,
        "last_played": 1700000100
      },
      {
        "id": 2,
        "character_id": 1,
        "mini_game": "tower_defense",
        "level_id": 2,
        "completed": false,
        "best_score": 0,
        "times_played": 0,
        "last_played": 0
      }
    ]
  },
  "token": "new-token-string"
}
```

### Success (200 OK) — qualified freeholder

A freeholder who has met the mesne-lord requirements (manor house level ≥ 6 +
the configured RTS milestone) is lazily promoted and the king's letter is sent
exactly once. The response then looks like:

```json
{
  "status": "ok",
  "data": {
    "character_id": 1,
    "game_phase": "freehold_qualified",
    "available_activities": ["tasks", "profile", "chat", "tournament", "adventure", "combat", "manor", "letters", "honor"],
    "base_unlocked": true,
    "has_letters": true,
    "unread_letter": { "id": 2, "letter_key": "letter_mesne_lord_eligible" },
    "barony_join_cooldown_until": 0,
    "progress": []
  }
}
```

Once `unread_letter` is present the hub shows the letter overlay; `markLetterRead`
clears it. The `honor` activity (shown in `freehold_qualified`) opens the
`/api/applyForMesneLord` form.

### Error

```json
{
  "status": "ok",
  "error": "character_id required"
}
```

## Fields

| Field | Type | Description |
|-------|------|-------------|
| `game_phase` | string | Progression phase (see the phase table below): `initial_mission`, `land_patent`, `baron_track`, `baron_right`, `freehold`, `freehold_qualified`, or `sandbox` |
| `available_activities` | array of strings | Activity IDs the hub should show for the current phase (see below) |
| `current_mini_game` | string or null | Active mini-game name if currently playing, else null |
| `current_level_id` | int or null | Active level ID if currently playing, else null |
| `base_unlocked` | boolean | Whether the player has earned the right to build a base (1 from `freehold` onward) |
| `has_letters` | boolean | True once the character has received at least one letter (hub Letters button) |
| `unread_letter` | object or absent | Present when an unread letter exists: `{ id, letter_key }` of the **oldest** unread letter (one-time overlay) |
| `barony_join_cooldown_until` | int | Unix timestamp the barony join cooldown expires; `0` when the character has never left a barony (or the cooldown is disabled) |
| `barony` | object or absent | Present while a barony member: `{ id, name, role, member_count }` |
| `progress` | array | Array of MiniGameProgress objects for all mini-games |

### `available_activities` by phase

The server controls which activities the hub grid exposes. During the `land_patent`
phase the `land_patent` activity replaces `manor`; the client renders it as the
land-patent decision panel (join a dukedom, found a freehold, or start the baron
track) instead of a generic activity card. After that, entries are added on top
of the phase's base set:

- `manor` is exposed whenever the character **owns a fiefdom** (i.e. from
  `freehold` onward, and in `sandbox`) — including `baron_track`/`baron_right`
  characters who already founded a freehold.
- `letters` is exposed once the character has received at least one letter.
- `honor` is exposed in `freehold_qualified` (opens `/api/applyForMesneLord`).
- `barony` is exposed while the character is a member of a barony.

| Phase | Base activities |
|-------|-----------------|
| `initial_mission` | `tasks`, `profile`, `chat` |
| `land_patent` | `tasks`, `profile`, `land_patent`, `chat`, `combat` |
| `baron_track` | `tasks`, `profile`, `chat`, `combat` |
| `baron_right` | `tasks`, `profile`, `chat`, `tournament`, `combat` |
| `freehold` | `tasks`, `profile`, `chat`, `tournament`, `adventure`, `combat` |
| `freehold_qualified` | `tasks`, `profile`, `chat`, `tournament`, `adventure`, `combat` |
| `sandbox` | `tasks`, `manor`, `profile`, `chat`, `tournament`, `adventure`, `combat` |

### Phase notes

- On each fetch a `freehold` character who meets the mesne-lord requirements
  (`progression.json` `mesne_lord.requirements`) is **lazily promoted** to
  `freehold_qualified`, and the one-time king's letter
  (`letter_mesne_lord_eligible`) is sent. The check is monotonic, so it covers
  the manor-upgrade and RTS-milestone paths without a background job.
- The `barony` object and `available_activities` `barony` entry are derived from
  `barony_members`; a barony that has been tombstoned (`baronies.defunct_at > 0`)
  no longer has members.

### MiniGameProgress Fields

| Field | Type | Description |
|-------|------|-------------|
| `mini_game` | string | Mini-game name (e.g. `"tower_defense"`) |
| `level_id` | int | Level number (1-9 for 3x3 grid) |
| `completed` | boolean | Whether this level has been won |
| `best_score` | int | Best score achieved on this level |
| `times_played` | int | Number of times this level has been attempted |
| `last_played` | int | Unix timestamp of last play attempt |
