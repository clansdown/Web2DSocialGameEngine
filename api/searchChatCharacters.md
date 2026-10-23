# `/api/searchChatCharacters`

Searches characters by display name (case-insensitive substring), for starting a
DM or inviting to a group. Returns at most 20 matches, excluding the caller.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "query": "bob" }
```

## Success Response

```json
{ "status": "ok", "data": { "characters": [
  { "character_id": 34, "display_name": "Bob", "portrait_url": "/api/avatar/34",
    "is_friend": true, "is_blocked": false } ] } }
```

## Notes

- Read-only; never creates a chat.
