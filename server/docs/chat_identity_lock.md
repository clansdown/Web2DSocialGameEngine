# Chat Identity Lock

Admins can lock and/or force-revert a character's **display name** and
**avatar** in response to abuse. This is a moderation tool, not a gameplay one.

## Data

`characters` gains four columns (migrated by `migrate_identity_lock` in
`server/init_db.cpp`):

| Column | Meaning |
|---|---|
| `name_locked` | 1 = the player cannot change their display name |
| `avatar_locked` | 1 = the player cannot change their avatar (safe or custom) |
| `identity_locked_by` | account id of the admin who applied the lock |
| `identity_locked_at` | unix time the lock was applied |

`character_identity_history` records prior values so an admin can revert to any
earlier one:

| Column | Meaning |
|---|---|
| `character_id` | the character |
| `kind` | `name` or `avatar` |
| `value` | the prior value (avatar values are safe-pool ids, or `custom`) |
| `changed_at` | unix time the value was replaced |

History rows are appended whenever a player changes their name/avatar (see
`handleUpdateCharacterProfile` and `handleSetCharacterAvatar`).

## Behaviour

- While `name_locked` is set, `updateCharacterProfile` rejects a `display_name`
  change with `{ "error": "name_locked" }`.
- While `avatar_locked` is set, `setCharacterAvatar`, `uploadCharacterAvatar`
  and `removeCustomAvatar` reject with `{ "error": "avatar_locked" }`.
- Locks are **permanent** until an admin clears them (an admin action can set
  the flags back to 0).

## Admin endpoints

- `adminGetIdentityHistory { character_id }` — the prior values, newest first.
- `adminLockIdentity { character_id, lock_name?, lock_avatar?, revert_name?,
  revert_avatar?, revert_to? }`:
  - `lock_name` / `lock_avatar` set the lock flags.
  - `revert_name` / `revert_avatar` replace the current value. `revert_to`
    selects `"prior"` (the previous history value) or `"safe"` (the default:
    `characters.safe_display_name` for names, the safe avatar pool for avatars).
  - Reverting without locking is possible but is **not** the default.
  - Every call appends to `moderation_log`.

Both endpoints require the caller's account id to be listed in
`game/config/chat_admins.json`.

## Deferred

Appeals route through a separate admin-contact subsystem (its own tab and
channels), which is deferred. The lock itself is applied by an admin directly.
