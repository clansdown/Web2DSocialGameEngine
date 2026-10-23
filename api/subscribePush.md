# `/api/subscribePush`

Registers a Web Push subscription for the caller's **account** (all its
characters). Idempotent per endpoint.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "endpoint": "https://fcm.googleapis.com/...", "p256dh": "B...", "auth": "..." }
```

## Success Response

```json
{ "status": "ok", "data": { "endpoint": "https://...", "subscribed": true } }
```

## Error Responses

```json
{ "error": "invalid_subscription" }
{ "error": "account_not_found" }
```
