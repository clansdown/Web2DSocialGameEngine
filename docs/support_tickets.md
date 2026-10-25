# Support Tickets

Support is an account-level, text-only ticket system shared by players and game
staff. It is independent of chat: a chat ban does not block support, and a
support lock does not block solo play.

## Tickets

`support_tickets`:

- `id`, `account_id`, optional `character_id`
- `category`: `appeal`, `report`, `technical`, `account`, `other`
- `subject` (1–160 characters)
- `status`: `open`, `awaiting_staff`, `awaiting_player`, `resolved`, `closed`
- `priority`: `normal`, `high`, `urgent`
- `assigned_account_id`
- `player_last_read_at`, `staff_last_read_at`
- timestamps

`support_ticket_messages` stores `author_kind` (`player`, `staff`, `system`),
author ids, body, and `is_internal`.

## Rules

- A player can create a ticket and reply while the ticket is not closed.
- A player reply moves the ticket to `awaiting_staff`; a non-internal staff
  reply moves it to `awaiting_player`.
- A player reply to a `resolved` ticket reopens it to `awaiting_staff`.
- `closed` is read-only.
- Support-locked accounts can list/read tickets and notifications but create and
  reply calls return `support_locked`.
- Staff can create a ticket proactively and can send a final message to a
  support-locked account.
- Internal staff notes are never returned to players.
- Unread badges are computed from the per-side read timestamps.
- Support tickets are a private staff channel; minors may write free text here
  (see `minor_content_visibility.md`). Ticket bodies are never copied into
  notifications.

## Journal links

`support_ticket_action_links` links a ticket to staff journal actions (for
example the moderation action under appeal). Staff ticket detail returns the
linked actions. `support.reply_ticket`, `support.internal_note`,
`support.create_ticket`, `support.set_ticket_status`, `support.assign_ticket`,
and `support.link_action` are journaled; communications are not reversible.

## Player UI

The hub shows a muted Support utility, not a gameplay card. Routes:

- `#/activity/support`
- `#/activity/support/<ticket_id>`
