# `/api/banFromChat`

Bans a character from a chat (owner/moderator only). Sets the member's
`left_at`, records the ban, and logs it. The owner cannot be banned.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "target_character_id": 34, "reason": "spam" }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "banned_character_id": 34 } }
```

## Error Responses

```json
{ "error": "not_permitted" }
{ "error": "cannot_ban_owner" }
{ "error": "character_not_found" }
{ "error": "chat_not_found" }
```
