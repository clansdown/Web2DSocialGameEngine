# `/api/pollChats`

Aggregate delta poll across direct/group/barony/dukedom chats (wildlands is
excluded — see `/api/pollWildlands`).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "cursor": 10432, "lang": "en" }
```

## Success Response

```json
{
  "status": "ok",
  "data": {
    "cursor": 10490, "has_more": false,
    "chats": [], "memberships": [], "members": [], "messages": [],
    "translations": [], "friend_events": [], "invites": [], "bans": [],
    "characters": {}
  }
}
```

Only rows with `change_id > cursor` are returned (capped at 200). `cursor`
advances to the max applied. Tombstones are returned with `deleted: true`.

Translation deltas carry the message id and tier:

```json
"translations": [
  { "message_id": "m_..", "body": "bonjour", "engine": "madlad400-3b",
    "language": "fr", "detected_source_language": "en", "confidence": 0.9,
    "change_id": 10491, "tier": "baseline" }
]
```

## Error Responses

```json
{ "error": "character_required" }
```

## Notes

- `memberships` are the caller's own rows; `members` are other members' rows for
  the caller's chats.
- A fresh device should use `getChats.cursor` as the starting cursor.
