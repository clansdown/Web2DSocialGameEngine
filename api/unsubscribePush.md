# `/api/unsubscribePush`

Removes a Web Push subscription by endpoint for the caller's account.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "endpoint": "https://fcm.googleapis.com/..." }
```

## Success Response

```json
{ "status": "ok", "data": { "endpoint": "https://...", "subscribed": false } }
```
