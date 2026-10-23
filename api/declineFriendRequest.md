# `/api/declineFriendRequest`

Declines a pending friend request (deletes it).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "target_character_id": 34 } }
```
