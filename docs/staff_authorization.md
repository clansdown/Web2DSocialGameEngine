# Staff Authorization

Ravenest has three independent authorization domains. Holding a role in one
never implies authority in another.

| Domain | Source | Scope |
| --- | --- | --- |
| Scoped chat moderation | `chat_members.role` | One chat (barony, group, etc.) |
| Chat-wide admin allowlist | `game/config/chat_admins.json` | Chat-only legacy surface |
| Game staff | `staff.db` | Accounts, identity, baronies, support, audit, unstick |

## Staff are normal accounts

A staff member is a normal `users` row plus a `staff_accounts` record and one or
more roles. Staff can play the game normally. Granting or revoking staff does
not create a new account type.

v1 grants/revokes staff exclusively through the `staff_admin` CLI. The staff
console is read-only for staff membership: a staff member cannot self-demote.

## Roles and capabilities

Starter roles are seeded in `staff.db`:

- `superadmin`
- `customer_service`
- `moderator`
- `avatar_moderator`
- `support_agent`

For v1 every starter role carries every capability. The capability checks are
already enforced server-side, so roles can be tightened later without a schema
change.

Capabilities:

- `account.view`, `account.set_adult_status`, `account.set_social_restriction`,
  `account.set_support_lock`
- `identity.view_history`, `identity.set_name_lock`,
  `identity.set_avatar_lock`, `identity.revert_name`, `identity.revert_avatar`,
  `identity.remove_custom_avatar`
- `avatar.ban_hash`
- `barony.view`, `barony.set_safe_identity`, `barony.set_accepts_minors`,
  `barony.reassign_owner`
- `chat.moderate_any`
- `support.ticket.view`, `support.ticket.reply`, `support.ticket.manage`,
  `support.ticket.internal_notes`
- `support.unstick_session`, `support.set_game_phase`,
  `support.set_march_progress`, `support.cancel_construction`,
  `support.adjust_fiefdom_resource`
- `staff.undo`, `staff.audit.read`, `staff.read_sensitive`
- `registration.invites.view`, `registration.invites.generate`,
  `registration.invites.revoke`

Every staff endpoint calls `staff::require_capability`; hiding a button in the
client is cosmetic only.

## Chat moderation

- Scoped chat roles keep their existing powers.
- `chat.moderate_any` lets staff view and moderate any chat, independent of the
  client's moderation-mode toggle.
- The toggle is persisted per account (`staff_preferences.chat_moderation_mode`)
  and only changes which controls the client shows.
- Staff chat views are journaled as `read.chat_messages` events.
- Every scoped moderation action (ban, unban, kick, delete message, set/clear
  moderator, rename group) is journaled in the shared staff journal.

## Account restrictions

- `users.social_restricted` blocks social interaction (chat sends, DMs, friend
  requests, group creation/invites) while leaving solo play available.
- `users.support_locked` blocks creating/replying to support tickets; the player
  can still read tickets and notifications, and staff can send a final message.
- The two flags are independent.

## CLI

```bash
staff_admin --db-dir <dir> grant <username|account_id> <role>
staff_admin --db-dir <dir> revoke <username|account_id>
staff_admin --db-dir <dir> list
staff_admin --db-dir <dir> roles
```

CLI changes are journaled with `actor_type=cli`, `authority=cli`, and the OS
user label.
