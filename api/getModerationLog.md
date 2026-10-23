# `/api/getModerationLog`

Returns recent moderation-log entries (newest first, up to 200). **Admins only**
(account id in `game/config/chat_admins.json`).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "chat_id": "c_ab.." }
```

`chat_id` is optional; when omitted the whole log is returned.

## Success Response

```json
{ "status": "ok", "data": { "entries": [
  { "id": 1, "created_at": 0, "actor_character_id": 12, "action": "ban",
    "chat_id": "c_ab..", "target_character_id": 34, "payload": { "reason": "spam" } } ] } }
```

## Error Responses

```json
{ "error": "not_admin" }
```
