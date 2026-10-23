# `/api/leaveChat`

Leaves a direct or group chat. Barony/dukedom/wildlands cannot be left this way.

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
{ "error": "cannot_leave_chat" }
{ "error": "not_a_member" }
{ "error": "chat_not_found" }
```

## Notes

- A leaving owner hands ownership to the latest poster (else the longest-standing
  member); an emptied group is archived. History is retained on rejoin.
