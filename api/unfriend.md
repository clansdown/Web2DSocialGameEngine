# `/api/unfriend`

Removes a mutual friendship (both directions).

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12, "target_character_id": 34 }
```

## Success Response

```json
{ "status": "ok", "data": { "target_character_id": 34 } }
```

## Notes

- Does not delete the direct chat thread.
