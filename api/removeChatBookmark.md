# `/api/removeChatBookmark`

Removes a private bookmark.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "message_id": "m_.." }
```

## Success Response

```json
{ "status": "ok", "data": { "message_id": "m_..", "bookmarked": false } }
```
