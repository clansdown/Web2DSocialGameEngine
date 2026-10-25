# `/api/adminGetDeletedMessages` (removed)

> **Deprecated/removed:** this endpoint is no longer registered. Use
> `/api/staffGetDeletedMessages`; authorization now comes from `staff.db`
> capabilities. See `api/staff.md`.

Admin-only forensic endpoint for messages deleted during an account demotion.
Normal readers never query this data.

## Request

```json
{
  "auth": { "username": "admin", "token": "..." },
  "limit": 100
}
```

## Response

```json
{
  "status": "ok",
  "data": {
    "messages": [
      {
        "id": 1,
        "message_id": "m_...",
        "chat_id": "c_...",
        "author_character_id": 12,
        "author_account_id": 4,
        "body": "original custom text",
        "safe_message_id": null,
        "gif_id": null,
        "source_language": "en",
        "reply_to_message_id": null,
        "reply_excerpt": "",
        "reply_safe_excerpt": "",
        "created_at": 1700000000,
        "deleted_at": 1700000100,
        "deletion_reason": "minor account demotion",
        "deleted_by_account_id": 1
      }
    ]
  }
}
```

## Errors

- `needs_auth`
- `not_admin`
