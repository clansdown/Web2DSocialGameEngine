# `/api/transferGroupOwnership`

Transfers group ownership. The caller must be the owner; the old owner is
demoted to moderator.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "owner_character_id": 34 } }
```

## Error Responses

```json
{ "error": "not_a_group" }
{ "error": "not_owner" }
{ "error": "not_a_member" }
```
