# `/api/sendChatMessage`

Posts a message. Enforces membership, bans, block/mute rules, per-chat length
limits, and per-account rate limiting. At most one GIF per message (exempt from
the character limit).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "body": "**hello**", "gif": null,
  "reply_to_message_id": "m_..", "language": "en", "client_id": "local-1" }
```

## Success Response

```json
{ "status": "ok", "data": { "id": "m_..", "chat_id": "c_ab..", "seq": 6,
  "author_character_id": 12, "body": "**hello**", "source_language": "en",
  "created_at": 0, "version": 1, "change_id": 10490, "reactions": [], "translation": null } }
```

## Error Responses

```json
{ "error": "empty_message" }
{ "error": "message_too_long" }
{ "error": "too_many_lines" }
{ "error": "blocked" }
{ "error": "banned_from_chat" }
{ "error": "not_a_member" }
{ "error": "rate_limited" }
```

## Notes

- Limits depend on the chat type (DM friend 4096 chars; wildlands 512/10 lines;
  barony/group 1024/20).
- The source language is detected by the translation sidecar, falling back to
  `language`.
- Delivery is via `/api/pollChats`; this response is for optimistic
  reconciliation.
