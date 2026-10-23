# `/api/renameGroup`

Renames a group. Owner only; rolling 24h cooldown.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "name": "New Name" }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "name": "New Name" } }
```

## Error Responses

```json
{ "error": "invalid_group_name" }
{ "error": "not_a_group" }
{ "error": "not_owner" }
{ "error": "name_change_cooldown" }
```
