# `/api/setGroupModerator`

Grants or revokes moderator status in a group. Owner only.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "target_character_id": 34, "moderator": true }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "moderator": true } }
```

## Error Responses

```json
{ "error": "not_a_group" }
{ "error": "not_owner" }
{ "error": "cannot_change_owner" }
{ "error": "not_a_member" }
```
