# Chat Identity Lock

Game staff can lock and/or force-revert a character's **display name** and
**avatar** in response to abuse. This is a moderation tool, not a gameplay one.
Authorization comes from `staff.db` capabilities (`identity.*`), not the legacy
`chat_admins.json` allowlist. Each change is journaled in `staff.db` and can be
undone while the captured identity still matches.

## Data

`characters` gains four columns (migrated by `migrate_identity_lock` in
`server/init_db.cpp`):

| Column | Meaning |
|---|---|
| `name_locked` | 1 = the player cannot change their display name |
| `avatar_locked` | 1 = the player cannot change their avatar (safe or custom) |
| `identity_locked_by` | account id of the staff member who applied the lock |
| `identity_locked_at` | unix time the lock was applied |

`character_identity_history` records prior values so staff can revert to any
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
- Locks are **permanent** until staff clears them (a staff action can set the
  flags back to 0).

## Staff endpoints

- `staffGetIdentityHistory { target_character_id }` — prior values, newest first.
  Requires `identity.view_history` and logs a sensitive read.
- `staffLockIdentity { character_id, lock_name?, lock_avatar?, revert_name?,
  revert_avatar?, revert_to?, reason? }`:
  - `lock_name` / `lock_avatar` set the lock flags.
  - `revert_name` / `revert_avatar` replace the current value. `revert_to`
    selects `"prior"` (the previous history value) or `"safe"` (the default:
    `characters.safe_display_name` for names, the safe avatar pool for avatars).
  - Reverting without locking is possible but is **not** the default.
  - The combined change is one journaled action (all-or-nothing undo).
- `staffRemoveCustomAvatar { character_id, reason }` clears the custom avatar
  reference and reverts to the safe pick. The stored object is retained so the
  action remains reversible; storage garbage collection is separate.
- `staffBanAvatarHash` / `staffUnbanAvatarHash` manage exact SHA-256 bans.

Authorization comes from `staff.db` capabilities (`identity.*`,
`avatar.ban_hash`), not `game/config/chat_admins.json`.

## Appeals

Appeals use the support ticket system (`docs/support_tickets.md`). Staff can
link the original journal action to the ticket, and undoing the action generates
a `moderation_reversal` notification.
