# `/api/setPushPreferences`

Sets the character's push preferences.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "push_enabled": true, "dm_from_friends": true, "group_mentions": false }
```

All fields optional.

## Success Response

```json
{ "status": "ok", "data": { "push_enabled": true, "dm_from_friends": true, "group_mentions": false } }
```

## Notes

- Defaults: push on, DMs from friends on, group mentions off.
