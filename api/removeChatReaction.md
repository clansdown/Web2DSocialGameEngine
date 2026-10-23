# `/api/removeChatReaction`

Removes the caller's emoji reaction from a message.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "message_id": "m_..", "emoji": "👍" }
```

## Success Response

```json
{ "status": "ok", "data": { "message_id": "m_..", "reactions": [], "change_id": 10501 } }
```

## Error Responses

```json
{ "error": "message_not_found" }
{ "error": "not_a_member" }
```
