# `/api/openDirectChat`

Opens (creating if needed) the direct chat between the caller and a target
character. Blocked pairs are rejected.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..",
  "chat": { "id": "c_ab..", "type": "direct", "direct_key": "12:34" },
  "target": { "display_name": "Bob", "portrait_url": "/api/avatar/34" } } }
```

## Error Responses

```json
{ "error": "cannot_dm_self" }
{ "error": "character_not_found" }
{ "error": "blocked" }
```

## Notes

- Direct chats are also created eagerly when a friendship is accepted.
