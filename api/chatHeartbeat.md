# `/api/chatHeartbeat`

Marks the character online (in-memory, 60s TTL). Presence is friends-only.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12 }
```

## Success Response

```json
{ "status": "ok", "data": { "ok": true } }
```

## Notes

- Never persisted; a restart drops everyone offline.
- Backgrounded clients that stop heartbeating are treated as offline.
