# `/api/setChatSettings`

Updates the caller's per-chat settings: `sticky` (pin), `muted` (chat-mute), and
`notify_level` (`all`/`mentions`/`none`).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "sticky": true, "muted": false, "notify_level": "mentions" }
```

All setting fields are optional.

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab.." } }
```

## Error Responses

```json
{ "error": "not_a_member" }
{ "error": "invalid_notify_level" }
```

## Notes

- A chat-mute suppresses all notifications, including @mentions.
