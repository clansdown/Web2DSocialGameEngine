# `/api/kickMember`

Removes a member from a group (owner/moderator only). A kick is not a ban; the
member may be re-invited.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "chat_id": "c_ab..", "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "chat_id": "c_ab.." } }
```

## Error Responses

```json
{ "error": "not_permitted" }
{ "error": "cannot_kick_owner" }
{ "error": "not_a_member" }
{ "error": "chat_not_found" }
```
