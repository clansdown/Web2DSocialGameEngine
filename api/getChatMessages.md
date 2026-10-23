# `/api/getChatMessages`

Paginated message history for one chat. Membership is required (wildlands is
open to all).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "before_seq": 100, "after_seq": 0, "limit": 50, "lang": "en" }
```

`before_seq` loads older messages (default: latest); `after_seq` loads newer.
`lang` selects the viewer translation included on each message.

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "messages": [ /* chat_message */ ], "has_more": true } }
```

## Error Responses

```json
{ "error": "not_a_member" }
{ "error": "chat_not_found" }
```

## Notes

- `limit` is clamped to 1..200 (default 50).
- Each message carries `reactions` and (when available) `translation`.
