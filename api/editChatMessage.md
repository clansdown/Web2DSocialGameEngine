# `/api/editChatMessage`

Edits a message. Author only. Records a `message_versions` row, bumps `version`,
and invalidates stored translations.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "message_id": "m_..", "body": "corrected text" }
```

## Success Response

```json
{ "status": "ok", "data": { "id": "m_..", "version": 2, "change_id": 10495 } }
```

## Error Responses

```json
{ "error": "message_not_found" }
{ "error": "not_author" }
{ "error": "empty_message" }
{ "error": "message_too_long" }
{ "error": "banned_from_chat" }
```

## Notes

- The edited message rides the normal message delta (same `id`, higher
  `change_id`).
