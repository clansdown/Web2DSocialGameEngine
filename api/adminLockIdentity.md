# `/api/adminLockIdentity` (removed)

> **Deprecated/removed:** this endpoint is no longer registered. Use
> `/api/staffLockIdentity`. See `api/staff.md`.

Locks and/or force-reverts a character's name/avatar. **Admins only.** See
`server/docs/chat_identity_lock.md`.

**Requires authentication.**

## Request

```json
{ "auth": { "username": "alice", "token": "..." }, "character_id": 12,
  "target_character_id": 34, "lock_name": true, "lock_avatar": true,
  "revert_name": true, "revert_avatar": false, "revert_to": "safe" }
```

`revert_to` is `"prior"` (previous history value) or `"safe"` (default).

## Success Response

```json
{ "status": "ok", "data": { "character_id": 34, "locked": true } }
```

## Error Responses

```json
{ "error": "not_admin" }
{ "error": "character_id_required" }
{ "error": "character_not_found" }
```

## Notes

- Every call appends to `moderation_log`.
- A locked name/avatar is rejected by the profile/avatar endpoints until an
  admin clears it.
