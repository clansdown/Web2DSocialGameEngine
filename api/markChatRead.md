# `/api/markChatRead`

Advances the character's read cursor for a chat and clears its unread count.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "chat_id": "c_ab..", "seq": 6 }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "last_read_seq": 6, "unread_count": 0 } }
```

## Error Responses

```json
{ "error": "not_a_member" }
```

## Notes

- `last_read_seq` only ever advances (`MAX`).
- Other devices see the change via the membership delta.
