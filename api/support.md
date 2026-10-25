# Support + notification endpoints

All player support and notification endpoints are authenticated POSTs and
operate on the authenticated account. Request bodies use the normal `auth`
wrapper. Success responses are `{ "status": "ok", "data": {...} }`; errors are
`{ "status": "ok", "error": "..." }`.

## `/api/getSupportContext`

Returns `{ support_locked, unread_tickets, unread_notifications }`.

## `/api/listSupportTickets`

Request: `{ status?, category?, query?, limit?, offset? }`

Response: `{ tickets: [...], support_locked, unread_notifications }`.

## `/api/getSupportTicket`

Request: `{ ticket_id }`

Response: `{ ticket, messages, support_locked }`. Player-visible messages only
(`is_internal = 0`). Marking the ticket read updates `player_last_read_at`.

Errors: `ticket_not_found`.

## `/api/createSupportTicket`

Request: `{ subject, body?, category, character_id? }`

`subject` is required (≤ 160 chars); `body` is optional. Categories: `appeal`,
`report`, `technical`, `account`, `other`.

Errors: `subject_required`, `subject_too_long`, `support_locked`,
`ticket_create_failed`, `message_create_failed`.

## `/api/replySupportTicket`

Request: `{ ticket_id, body }`

A player reply moves the ticket to `awaiting_staff`; replying to `resolved`
reopens it. Errors include `ticket_not_found`, `ticket_closed`,
`support_locked`, `body_required`.

## `/api/reopenSupportTicket`

Request: `{ ticket_id }`. Reopens a non-closed ticket to `awaiting_staff`.

## `/api/getNotifications`

Request: `{ limit?, offset? }`

Response:

```json
{
  "notifications": [
    {
      "id": "ntf_...",
      "kind": "support_reply",
      "text_id": "ui_notification_support_reply",
      "text_args": { "subject": "..." },
      "ticket_id": "tkt_...",
      "action_id": "",
      "route": "#/activity/support/tkt_...",
      "created_at": 1700000000,
      "read_at": 0,
      "read": false
    }
  ],
  "unread": 1,
  "support_locked": false
}
```

## `/api/markNotificationRead`

Request: `{ notification_id }`; response `{ unread }`.

## `/api/markAllNotificationsRead`

Response `{ unread: 0 }`.

## Notes

- Support tickets are a private staff channel; minors may write free text here
  (the documented exception to minor safe-content authoring). Bodies are never
  copied into notifications.
- A support-locked account can still read tickets and notifications.
- See `docs/support_tickets.md` and `docs/notifications.md`.
