# `/api/muteCharacter`

Person-mutes a character: no notifications from them, and their group messages
are hidden by default.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "target_character_id": 34, "muted": true } }
```

## Error Responses

```json
{ "error": "cannot_mute_self" }
{ "error": "character_not_found" }
```
