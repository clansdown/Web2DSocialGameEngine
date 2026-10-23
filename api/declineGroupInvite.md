# `/api/declineGroupInvite`

Declines a pending group invite.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "chat_id": "c_ab.." }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab.." } }
```
