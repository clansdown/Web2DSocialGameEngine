# `/api/getNotifications`

Returns the authenticated account's system-generated notification inbox,
newest first. Notifications persist until read.

**Authentication:** Required

## Request

```json
{
  "auth": { "username": "player1", "token": "existing-token" },
  "limit": 50,
  "offset": 0
}
```

## Response

```json
{
  "status": "ok",
  "data": {
    "notifications": [
      {
        "id": "ntf_...",
        "account_id": 1,
        "character_id": 2,
        "kind": "moderation_action",
        "text_id": "ui_notification_chat_ban",
        "text_args": { "character_name": "SocratesKnight", "chat_name": "Wildlands" },
        "ticket_id": "",
        "action_id": "act_...",
        "route": "#/activity/support",
        "created_at": 1700000000,
        "read_at": 0,
        "read": false
      }
    ],
    "unread": 1,
    "support_locked": false
  }
}
```

## Fields

| Field | Type | Description |
| --- | --- | --- |
| `kind` | string | `moderation_action`, `moderation_reversal`, `social_restriction`, `support_lock`, `support_reply`, `support_ticket`, or `staff_notice` |
| `text_id` | string | Text-system key; the client renders it and substitutes `text_args` |
| `text_args` | object | Token substitutions (`character_name`, `chat_name`, `subject`, ...) |
| `ticket_id` | string | Linked support ticket (empty when none) |
| `action_id` | string | Linked staff journal action (empty when none) |
| `route` | string | Client hash route for the notification |
| `read` | boolean | `read_at > 0` |

## Notes

- A support-locked or socially restricted account can still read notifications.
- Notifications never contain custom avatar keys or support free-text bodies.
- `markNotificationRead` and `markAllNotificationsRead` update unread state.
- See `docs/notifications.md` and `server/tables/notifications.md`.
