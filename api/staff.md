# Staff console endpoints

All staff endpoints are authenticated POSTs. Authorization is checked
server-side with `staff::require_capability`; the staff console UI only hides
tools. A non-staff account receives `{ "error": "not_staff" }`; a staff account
missing a capability receives `{ "error": "missing_capability:<name>" }`.

Every mutating endpoint returns an `action_id`. Mutation effects are journaled
and reversible only when the journal preflight allows it. Grant/revoke is CLI
only (`staff_admin`), never an HTTP endpoint.

## Context

### `/api/staffGetContext`

Response: `{ is_staff, account_id, roles, capabilities, unread_notifications,
support_unread, chat_moderation_mode, roles_catalog }`.

### `/api/staffListStaff`

Read-only staff roster (account id, username, roles, timestamps). Requires
`staff.audit.read`. Grant/revoke remains CLI-only.

### `/api/staffSetChatModerationMode`

Request `{ enabled }`; requires `chat.moderate_any`. Response
`{ chat_moderation_mode }`. This persists the opt-in UI toggle only.

## Accounts

- `/api/staffSearchAccounts` — `{ query }` → `{ accounts }` (username/character
  search; each account includes characters).
- `/api/staffGetAccount` — `{ account_id | username }` → account record with
  characters. Logs `read.account`.
- `/api/staffSetAdultStatus` — `{ account_id | username, adult, method?, reason }`.
  Setting `adult=false` runs the demotion scrub and is not undoable.
- `/api/staffSetSocialRestriction` — `{ account_id, restricted, reason }`.
- `/api/staffSetSupportLock` — `{ account_id, locked, reason }`.

## Identity / avatars

- `/api/staffGetIdentityHistory` — `{ target_character_id }` → `{ history }`.
- `/api/staffLockIdentity` — `{ character_id, lock_name?, lock_avatar?,
  revert_name?, revert_avatar?, revert_to?, reason }`.
- `/api/staffRevertIdentity` — shorthand for revert flags.
- `/api/staffRemoveCustomAvatar` — `{ character_id, reason }`.
- `/api/staffBanAvatarHash` — `{ sha256, reason }` (64 hex chars).
- `/api/staffUnbanAvatarHash` — `{ sha256, reason }`.
- `/api/staffListAvatarHashBans` — `{ include_revoked? }` → `{ bans }`.

## Baronies

- `/api/staffSearchBaronies` — `{ query }` → `{ baronies }`.
- `/api/staffSetBaronyIdentity` — `{ barony_id, safe_name_id, safe_tag_ids,
  name?, description?, reason }`; safe ids are validated against the safe
  catalogs.
- `/api/staffSetBaronyAcceptsMinors` — `{ barony_id, accepts_minors, reason }`
  (rejects disabling while a minor is a member).
- `/api/staffReassignBaronyOwner` — `{ barony_id, owner_character_id, reason }`.

## Support queue

- `/api/staffListSupportQueue` — `{ status?, category?, query?, limit?, offset? }`
  → `{ tickets, unread }`.
- `/api/staffGetSupportTicket` — `{ ticket_id }` → `{ ticket, messages, actions }`
  (messages include internal notes; logs a read event).
- `/api/staffReplySupportTicket` — `{ ticket_id, body, internal }`.
- `/api/staffCreateSupportTicket` — `{ account_id, subject, body?, category,
  character_id? }`.
- `/api/staffSetSupportTicketStatus` — `{ ticket_id, status }`.
- `/api/staffAssignSupportTicket` — `{ ticket_id, assigned_account_id }` (0
  clears).
- `/api/staffLinkSupportAction` — `{ ticket_id, action_id }`.

## Audit / undo

- `/api/staffListJournal` — filters: `action_type`, `actor_account_id`,
  `target_type`, `target_id`, `status`, `operation`, `query`, `limit`, `offset`.
  Logs `read.staff_journal`.
- `/api/staffGetJournalEntry` — `{ action_id }` → `{ entry, preflight }`.
- `/api/staffPreflightUndo` — `{ action_id }` → `{ status, reason, resources }`.
  Statuses: `available`, `target_missing`, `target_changed`, `not_reversible`,
  `already_undone`, `blocked`.
- `/api/staffUndo` — `{ action_id, reason }`; requires `staff.undo`.
- `/api/staffRecoverPendingAction` — marks leftover pending actions `partial`.

## Unstick

- `/api/staffUnstickGameSession` — `{ character_id, reason }`.
- `/api/staffSetGamePhase` — `{ character_id, game_phase, reason }`.
- `/api/staffSetMarchProgress` — `{ character_id, mini_game, next_level, reason }`.
- `/api/staffCancelConstruction` — `{ building_id, reason }` (only while
  `construction_start_ts > 0`).
- `/api/staffAdjustFiefdomResource` — `{ fiefdom_id, resource, value, reason }`
  (non-negative).

## Chat

- `/api/staffSearchChats` — `{ query }` → `{ chats }` by id/name.
- `/api/staffGetDeletedMessages` — `{ limit? }` → `{ messages }` (forensic
  demotion copies); logs `read.deleted_messages`.

## Migration note

The legacy `adminSetAdultStatus`, `adminSetBaronyIdentity`,
`adminGetDeletedMessages`, `adminGetIdentityHistory`, and `adminLockIdentity`
endpoints are no longer registered. Use the `staff*` endpoints above; their
authorization comes from `staff.db`, not `chat_admins.json`.
