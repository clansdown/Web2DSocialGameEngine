# `/api/unbanFromChat`

Lifts a chat ban (owner/moderator only). Logs the action.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "unbanned_character_id": 34 } }
```

## Error Responses

```json
{ "error": "not_permitted" }
```
