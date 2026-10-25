# staff.* tables (`staff.db`)

The staff database is independent of `game.db` and `chat.db`. No cross-database
foreign keys exist; ids are plain integers/strings.

## `staff_roles`

| Column | Type | Notes |
| --- | --- | --- |
| id | TEXT PK | e.g. `superadmin` |
| name | TEXT UNIQUE | display name |
| description | TEXT | purpose |
| created_at | INTEGER | informational |

Seeded roles: `superadmin`, `customer_service`, `moderator`,
`avatar_moderator`, `support_agent`.

## `staff_role_capabilities`

`(role_id, capability)` primary key. v1 seeds every capability on every starter
role. Capability strings are listed in `docs/staff_authorization.md`.

## `staff_accounts`

| Column | Type | Notes |
| --- | --- | --- |
| account_id | INTEGER PK | `users.id` in game.db |
| created_at | INTEGER | grant time |
| created_by_label | TEXT | CLI/OS label |
| revoked_at | INTEGER | NULL while active |

## `staff_account_roles`

`(account_id, role_id)` primary key. Multiple roles are allowed; capabilities
are the union.

## `staff_preferences`

`account_id` PK plus `chat_moderation_mode` (0/1) and `updated_at`. The toggle is
a UI preference only; `chat.moderate_any` is always enforced server-side.

## `staff_action_journal`

The shared append-only audit journal. See `docs/staff_action_journal.md` for the
full lifecycle. Key columns: `operation` (`apply`/`undo`/`read`), actor fields,
`action_type`, `action_version`, target, `scope`, `reason`, `before_state`,
`after_state`, `undo_descriptor`, `status`, `undo_of`, timestamps.

## `staff_action_events`

Append-only transitions: `action_id`, `event_type`, `detail`, `created_at`.

## `staff_action_links`

`(action_id, resource_type, resource_id, link_role)` primary key. Associates an
action with its primary/effect/ticket/notification resources.
