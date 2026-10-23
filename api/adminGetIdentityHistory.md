# `/api/adminGetIdentityHistory`

Returns a character's prior names/avatars for admin review. **Admins only.**

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "character_id": 34, "history": [
  { "id": 5, "kind": "name", "value": "OldName", "changed_at": 0 } ] } }
```

## Error Responses

```json
{ "error": "not_admin" }
```
