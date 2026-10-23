# `/api/inviteToGroup`

Invites a character to a group. Owner/moderators may always invite; other
members only when the group's `members_can_invite` policy is on. The invitee
must accept.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab..", "invited_character_id": 34 } }
```

## Error Responses

```json
{ "error": "not_a_group" }
{ "error": "not_a_member" }
{ "error": "not_permitted" }
{ "error": "already_member" }
{ "error": "character_not_found" }
{ "error": "blocked" }
{ "error": "banned_from_chat" }
{ "error": "invite_limit" }
```
