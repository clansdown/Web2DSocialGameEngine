# `/api/pollWildlands`

Delta poll for the single global wildlands chat. Called only while the wildlands
view is open.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "cursor": 0, "lang": "en" }
```

## Success Response

```json
{ "status": "ok", "data": { "cursor": 220, "has_more": false, "messages": [], "translations": [], "characters": {} } }
```

## Notes

- Wildlands never contributes to `pollChats`, has no unread badge, and never
  triggers push.
- Messages are global; no membership is required to read or post.
