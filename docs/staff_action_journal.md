# Staff Action Journal

`staff.db` holds one append-only journal for game-staff actions **and** scoped
chat moderation actions. The journal is staff-only; there is no player-facing
action history.

## Why a fourth database

An action can mutate `game.db` (accounts, characters, baronies, fiefdoms) and
`chat.db` (bans, messages, chat names). A journal in either database could not
faithfully link both sides. `staff.db` is independent and has no cross-database
foreign keys; ids are stored as plain values.

## Entry shape

`staff_action_journal` records:

- `operation`: `apply`, `undo`, or `read`
- actor (`actor_account_id`, `actor_character_id`, `actor_type`, `authority`)
- capability and action type + version
- target (`target_type`, `target_id`) and optional `scope`
- reason
- `before_state`, `after_state`, `undo_descriptor` (JSON)
- `status`: `pending`, `applied`, `failed`, `partial`, `undone`,
  `not_reversible`
- `parent_action_id`, `undo_of`
- timestamps

`staff_action_events` is the append-only transition log (`created`, `applied`,
`failed`, `partial`, `preflight`, `undo_applied`, `undo_failed`).
`staff_action_links` associates an action with its resources.

## Apply lifecycle

1. Validate the staff capability.
2. Capture the before-state.
3. Append a `pending` entry.
4. Apply the target effect.
5. Capture the after-state and undo descriptor.
6. Mark `applied` or `failed`.
7. Emit system notifications.
8. Link resources/tickets.

If the process dies mid-action, `recover_interrupted_actions` marks pending rows
`partial` with `interrupted; verification required` so they surface in the staff
console.

## Undo

- Undo creates a new compensating entry (`operation=undo`, `undo_of=<original>`).
  It never rewrites the original row.
- Only `staff.undo` is required; there is no force-undo and no partial undo.
- Preflight returns one of: `available`, `target_missing`, `target_changed`,
  `not_reversible`, `already_undone`, `blocked`.
- Composite actions (for example identity lock + revert) are all-or-nothing.
- An action can be undone once. There is no undo-of-undo.
- Undo emits a `moderation_reversal` notification linked to the undo entry.

Irreversible by design: adult-status changes that demote an account (demotion
scrubs identity and permanently tombstone-deletes messages; only the forensic
copy remains). The journal records the action but preflight reports
`not_reversible`.

## Chat moderation

Scoped moderation actions are written by `ChatService` through
`staff::record_scoped_chat_action` with `actor_type=chat_member` and
`authority=chat_role`. Staff-wide moderation uses the same journal. Chat undo
restores the captured state (ban row, membership `left_at`, deleted message body,
member role, or group name) and fails if the target no longer exists.

## Sensitive reads

Read events use `operation=read` and are never undoable:
`read.account`, `read.identity_history`, `read.custom_avatar`,
`read.deleted_messages`, `read.support_ticket`, `read.staff_journal`,
`read.chat_messages`, `read.barony`.
