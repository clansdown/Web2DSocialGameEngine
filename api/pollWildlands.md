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
- `translations` entries use the same `{message_id, body, engine, language,
  detected_source_language, confidence, change_id, tier}` shape as
  `pollChats`.
