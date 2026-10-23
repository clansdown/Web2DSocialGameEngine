# `/api/addChatBookmark`

Bookmarks a message privately for the caller.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "message_id": "m_.." }
```

## Success Response

```json
{ "status": "ok", "data": { "message_id": "m_..", "bookmarked": true } }
```

## Error Responses

```json
{ "error": "message_not_found" }
```
