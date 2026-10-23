# `/api/getChatBookmarks`

Lists the caller's bookmarked messages (newest first, up to 200).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12 }
```

## Success Response

```json
{ "status": "ok", "data": { "messages": [ /* chat_message */ ] } }
```
