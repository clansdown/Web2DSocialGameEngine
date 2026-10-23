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

## Error Responses

```json
{ "error": "character_required" }
```

## Notes

- `memberships` are the caller's own rows; `members` are other members' rows for
  the caller's chats.
- A fresh device should use `getChats.cursor` as the starting cursor.
