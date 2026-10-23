# `/api/sendFriendRequest`

Sends a friend request. Rejected when blocked either way, already friends, or
over the daily request cap.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "target_character_id": 34, "change_id": 10510 } }
```

## Error Responses

```json
{ "error": "cannot_friend_self" }
{ "error": "character_not_found" }
{ "error": "blocked" }
{ "error": "already_friends" }
{ "error": "already_requested" }
{ "error": "friend_request_limit" }
```
