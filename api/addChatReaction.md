# `/api/addChatReaction`

Adds a server-catalog reaction to a message. Arbitrary emoji are not accepted.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "message_id": "m_..", "reaction_id": "thumbs_up" }
```

## Success Response

```json
{ "status": "ok", "data": { "message_id": "m_..",
  "reactions": [ { "reaction_id": "thumbs_up", "emoji": "👍", "character_ids": [12] } ],
  "change_id": 10500 } }
```

## Error Responses

```json
{ "error": "invalid_reaction" }
{ "error": "reaction_not_minor_safe" }
{ "error": "message_not_visible" }
{ "error": "message_not_found" }
{ "error": "not_a_member" }
{ "error": "blocked" }
```

## Notes

- The id comes from `/api/getSafeContent`; minors receive only `minor_safe`
  reactions there, and the server re-checks the flag.
- Bumps the parent message's `change_id`; no separate reaction cursor.
