# `/api/blockUser`

Blocks a character. Bidirectional: prevents DMs, @mentions, friend requests,
group invites, and reactions to each other's messages, and hides messages both
ways. Removes any friendship/requests.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "target_character_id": 34, "blocked": true } }
```

## Error Responses

```json
{ "error": "cannot_block_self" }
{ "error": "character_not_found" }
```
