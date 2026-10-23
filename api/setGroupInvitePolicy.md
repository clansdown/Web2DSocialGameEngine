# `/api/setGroupInvitePolicy`

Sets whether ordinary members may invite. Owner only.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "members_can_invite": true }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "members_can_invite": true } }
```

## Error Responses

```json
{ "error": "not_a_group" }
{ "error": "not_owner" }
```
