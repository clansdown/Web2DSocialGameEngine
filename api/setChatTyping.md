# `/api/setChatTyping`

Sets a ~5s typing flag for the character in a chat. Also refreshes presence.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "chat_id": "c_ab.." }
```

## Success Response

```json
{ "status": "ok", "data": { "ok": true } }
```

## Notes

- Delivered to friends via `/api/getFriendPresence`.
