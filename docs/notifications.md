# Notifications

Notifications are account-level, system-generated, and unobtrusive. They are
not a gameplay card and are not a place for staff-authored explanations — those
belong in support tickets.

## Storage

`staff.db` `notifications`:

- `id`, `account_id`, optional `character_id`
- `kind`: `moderation_action`, `moderation_reversal`, `social_restriction`,
  `support_lock`, `support_reply`, `support_ticket`, `staff_notice`
- `text_id` + `text_args` (the client resolves localized text)
- optional `ticket_id`, `action_id`, `route`
- `created_at`, `read_at`

## Rules

- Persist until read; read notifications remain in the inbox.
- Mark one or all read.
- A restricted or support-locked account can still read notifications.
- Names are the character's safe display name. Notifications never contain
  custom avatar keys or ticket free-text bodies.
- Generated for: adult-status changes, social restrictions, support locks,
  name/avatar moderation, barony identity changes, chat bans/kicks, message
  deletion, support replies, staff-created tickets, support/unstick notices, and
  reversal of a moderation action.
- Reversal notifications link to the compensating journal entry and the support
  surface.

## Text ids

`ui_notification_*` files live in `game/text/en/`. The client substitutes
`{key}` tokens from `text_args`.
