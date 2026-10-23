# `/api/getFriends`

Returns the caller's friend graph: accepted friends, incoming/outgoing
requests, blocked and muted character ids, and the referenced character
directory.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12 }
```

## Success Response

```json
{ "status": "ok", "data": {
  "friends": [ { "character_id": 34, "since": 0 } ],
  "incoming_requests": [ { "character_id": 56, "requested_at": 0 } ],
  "outgoing_requests": [],
  "blocked_character_ids": [],
  "muted_character_ids": [],
  "characters": { "34": { "display_name": "Bob", "portrait_url": "/api/avatar/34" } } } }
```

## Notes

- Friendship is mutual; requests are one-directional until accepted.
