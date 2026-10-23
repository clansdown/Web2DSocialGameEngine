# `/api/getFriendPresence`

Returns online/typing presence for the caller's friends.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12 }
```

## Success Response

```json
{ "status": "ok", "data": { "friends": [
  { "character_id": 34, "online": true, "typing": ["c_ab.."] } ] } }
```

## Notes

- In-memory only; friends-only.
