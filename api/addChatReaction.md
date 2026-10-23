# `/api/addChatReaction`

Adds an emoji reaction to a message. Any Unicode emoji is allowed (≤ 8 code
points). Blocked interaction is rejected.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "message_id": "m_..", "emoji": "👍" }
```

## Success Response

```json
{ "status": "ok", "data": { "message_id": "m_..",
  "reactions": [ { "emoji": "👍", "character_ids": [12] } ], "change_id": 10500 } }
```

## Error Responses

```json
{ "error": "invalid_emoji" }
{ "error": "message_not_found" }
{ "error": "not_a_member" }
{ "error": "blocked" }
```

## Notes

- Bumps the parent message's `change_id`; no separate reaction cursor.
