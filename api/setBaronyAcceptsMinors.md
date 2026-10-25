# `/api/setBaronyAcceptsMinors`

Sets a barony's `accepts_minors` flag. The barony owner or an admin may call it.
It may not be set to `false` while a minor is a member.

## Request

```json
{
  "auth": { "username": "alice", "token": "..." },
  "barony_id": 7,
  "accepts_minors": false
}
```

## Response

```json
{
  "status": "ok",
  "data": {
    "barony_id": 7,
    "accepts_minors": false
  }
}
```

## Errors

- `needs_auth`
- `barony_id required`
- `accepts_minors boolean required`
- `barony not found`
- `not_barony_owner`
- `barony_has_minor_members`
