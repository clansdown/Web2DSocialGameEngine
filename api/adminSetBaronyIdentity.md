# `/api/adminSetBaronyIdentity` (removed)

> **Deprecated/removed:** this endpoint is no longer registered. Use
> `/api/staffSetBaronyIdentity`; authorization now comes from `staff.db`
> capabilities. See `api/staff.md`.

Admin-only endpoint to reassign a barony's mandatory safe name/tags and
optionally replace its adult custom name/description. Used for support and
content cleanup.

## Request

```json
{
  "auth": { "username": "admin", "token": "..." },
  "barony_id": 7,
  "safe_name_id": "oakhaven",
  "safe_tag_ids": ["laid_back", "new_player_friendly"],
  "name": "Oakhaven",
  "description": "A quiet frontier holding"
}
```

## Response

```json
{
  "status": "ok",
  "data": {
    "barony_id": 7,
    "safe_name_id": "oakhaven",
    "safe_tag_ids": ["laid_back", "new_player_friendly"],
    "name": "Oakhaven"
  }
}
```

## Errors

- `needs_auth`
- `not_admin`
- `barony_id and valid safe_name_id required`
- `invalid_safe_barony_tag`
- `barony not found`

## Notes

- Safe ids come from `/api/getSafeContent`.
- If `name` is omitted/empty, the safe label is stored so the unique display
  name remains valid.
- Minors always see the safe name/tags regardless of the custom values.
