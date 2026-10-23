# `/api/acceptFriendRequest`

Accepts a pending friend request. Creates the mutual friendship and eagerly
creates the direct chat.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "friend_character_id": 34, "chat_id": "c_ab.." } }
```

## Error Responses

```json
{ "error": "no_pending_request" }
{ "error": "blocked" }
```
