# `/api/sendChatMessage`

Posts a message. Enforces membership, bans, block/mute rules, per-chat length
limits, and per-account rate limiting.

**Requires authentication.**

## Request

Custom message (adults only):

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "body": "**hello**", "gif_id": null,
  "reply_to_message_id": "m_..", "language": "en", "client_id": "local-1" }
```

Safe message (all ages):

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "safe_message_id": "greeting_hello",
  "reply_to_message_id": "m_..", "language": "en" }
```

## Success Response

```json
{ "status": "ok", "data": { "id": "m_..", "chat_id": "c_ab..", "seq": 6,
  "author_character_id": 12, "body": "**hello**", "safe_message_id": null,
  "source_language": "en", "created_at": 0, "version": 1, "change_id": 10490,
  "reactions": [], "translation": null } }
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
{ "error": "minor_custom_content_forbidden" }
{ "error": "invalid_safe_message" }
{ "error": "invalid_safe_message_context" }
{ "error": "invalid_gif" }
{ "error": "gif_not_minor_safe" }
{ "error": "group_adults_only" }
```

## Notes

- Minors may send only a `safe_message_id` (and optionally a `minor_safe`
  `gif_id`). A non-empty `body` from a minor is rejected.
- The safe message's `contexts` array must include the chat type.
- GIFs are server-catalog IDs, not arbitrary JSON. A minor may use only
  `minor_safe` GIFs.
- Safe messages are not sent through the translation pipeline.
- Hidden custom messages are omitted from minor reads and do not affect their
  unread counts or push.
- Delivery is via `/api/pollChats`; this response is for optimistic
  reconciliation.
