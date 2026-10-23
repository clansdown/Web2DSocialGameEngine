# `/api/getPushConfig`

Returns the VAPID public key for Web Push subscription.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12 }
```

## Success Response

```json
{ "status": "ok", "data": { "public_key": "B...", "enabled": true, "key_id": "dev" } }
```

## Notes

- `enabled` is false when no VAPID key is configured; the client then skips
  subscription.
