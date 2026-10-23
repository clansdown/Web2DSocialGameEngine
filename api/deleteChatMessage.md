# `/api/deleteChatMessage`

Deletes a message (tombstone). Author, or a moderator/owner of the chat.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "message_id": "m_.." }
```

## Success Response

```json
{ "status": "ok", "data": { "id": "m_..", "change_id": 10496 } }
```

## Error Responses

```json
{ "error": "message_not_found" }
{ "error": "not_author" }
```

## Notes

- The body is set to NULL and `deleted_at` is stamped; the row is retained.
- Clients render a "message deleted" placeholder from the tombstone.
