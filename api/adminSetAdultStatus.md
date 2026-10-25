# `/api/adminSetAdultStatus` (removed)

> **Deprecated/removed:** this endpoint is no longer registered. Use
> `/api/staffSetAdultStatus`; authorization now comes from `staff.db`
> capabilities, not `game/config/chat_admins.json`. See `api/staff.md`.

Admin/customer-service-only endpoint that sets an account's adult status.
Setting it to `false` performs the full demotion scrub and deletes all messages
authored by every character on the account (retaining admin-only forensic
copies). There is no client-callable age override.

## Request

```json
{
  "auth": { "username": "admin", "token": "..." },
  "account_id": 42,
  "adult": false,
  "method": "customer_service",
  "reason": "Verified minor account"
}
```

`target_username` may be supplied instead of `account_id`.

## Response

```json
{
  "status": "ok",
  "data": {
    "account_id": 42,
    "adult": false,
    "method": "customer_service"
  }
}
```

## Errors

- `needs_auth` — no session.
- `not_admin` — caller is not in `chat_admins.json`.
- `account_id or target_username required`
- `adult boolean required`

## Demotion effects

- custom display names/avatars are cleared;
- custom honor/barony text is scrubbed and baronies force `accepts_minors`;
- group memberships are removed and owned groups transfer/archive;
- authored messages are tombstoned and copied to `message_deletions`;
- the action is recorded in the moderation log.
