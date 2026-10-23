# `/api/acceptGroupInvite`

Accepts a pending group invite. Enforces the member cap and rejects banned
characters.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "chat_id": "c_ab.." }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab.." } }
```

## Error Responses

```json
{ "error": "no_invite" }
{ "error": "invite_expired" }
{ "error": "banned_from_chat" }
{ "error": "group_full" }
```
