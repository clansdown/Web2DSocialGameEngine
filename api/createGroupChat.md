# `/api/createGroupChat`

Creates a group chat owned by the caller. Enforces the per-character group cap.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "name": "Test Warband" }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "name": "Test Warband" } }
```

## Error Responses

```json
{ "error": "invalid_group_name" }
{ "error": "group_limit" }
```

## Notes

- The creator becomes `owner`. Renaming later is owner-only with a 24h cooldown.
