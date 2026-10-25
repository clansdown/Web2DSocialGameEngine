# Invitation Endpoints

Player and staff endpoints for single-use registration invitation codes. The
feature is configured by `game/config/registration.json` (see
`docs/registration.md`). Player codes are stored in the lazily-opened
`invites.db`; the redemption ledger is `users.invitation_code_id` in `game.db`.

---

## PLAYER

### `/api/getInvitationStatus`

Returns the authenticated player's generation allowance and their own codes
(plaintext, including used/revoked state).

#### Request

```json
{ "auth": { "username": "player", "token": "..." } }
```

#### Response

```json
{
  "status": "ok",
  "data": {
    "mode": "invite_only",
    "invite_required": true,
    "players_can_generate": true,
    "enabled": true,
    "can_generate": true,
    "daily_limit": 3,
    "lifetime_limit": 10,
    "generated_today": 1,
    "generated_total": 4,
    "daily_remaining": 2,
    "lifetime_remaining": 6,
    "codes": [
      {
        "id": "inv_1700000000_ab12cd34ef56gh78",
        "code": "ABCD-EFGH-JKLM",
        "created_by_kind": "player",
        "created_at": 1700000000,
        "used": false,
        "used_at": 0,
        "revoked": false,
        "revoked_at": 0
      }
    ]
  }
}
```

In `off` or `global_code` mode, `enabled`/`can_generate` are false and `codes`
is empty (the player utility is hidden).

#### Errors

- `needs_auth` — missing/invalid session
- `account_not_found`

---

### `/api/createInvitationCode`

Generates one new single-use code for the authenticated player.

#### Request

```json
{ "auth": { "username": "player", "token": "..." } }
```

#### Response

```json
{
  "status": "ok",
  "data": {
    "invitation": {
      "id": "inv_...",
      "code": "ABCD-EFGH-JKLM",
      "created_by_kind": "player",
      "created_at": 1700000000,
      "used": false,
      "used_at": 0,
      "revoked": false,
      "revoked_at": 0
    },
    "code": "ABCD-EFGH-JKLM"
  }
}
```

#### Errors

- `invite_generation_disabled` — mode is not `invite_only`, the global flag is
  off, or either player limit is `0`
- `invite_daily_limit_reached`
- `invite_lifetime_limit_reached`
- `needs_auth`

Players cannot revoke codes.

---

## STAFF

Staff endpoints require an active staff record with the named capability. All
actions enter the shared journal (`staff.db`). **Plaintext codes are never
written to the journal**, because `invites.db` may later be deleted.

### `/api/staffListInvitationCodes`

Capability: `registration.invites.view`. Logs `read.invitation_codes`.

#### Request

```json
{
  "auth": { "username": "staff", "token": "..." },
  "status": "all",
  "created_by_kind": "",
  "query": "",
  "limit": 100,
  "offset": 0
}
```

`status` is `all`, `unused`, `used`, or `revoked`.

#### Response

```json
{
  "status": "ok",
  "data": {
    "mode": "invite_only",
    "invitations_enabled": true,
    "codes": [
      {
        "id": "inv_...",
        "code": "ABCD-EFGH-JKLM",
        "created_by_kind": "staff",
        "created_by_account_id": 1,
        "created_by_username": "staff",
        "created_at": 1700000000,
        "used": false,
        "used_at": 0,
        "used_by_account_id": 0,
        "used_by_username": "",
        "revoked": false,
        "revoked_at": 0,
        "revoked_by_account_id": 0
      }
    ]
  }
}
```

`invitations_enabled` is false in `off` mode; `codes` is then empty and
`invites.db` is not opened.

---

### `/api/staffCreateInvitationCode`

Capability: `registration.invites.generate`. Allowed only in `invite_only`.
Staff generation is unlimited. Journal action: `registration.invite_generate`.

#### Request

```json
{
  "auth": { "username": "staff", "token": "..." },
  "reason": "new player onboarding"
}
```

#### Response

```json
{
  "status": "ok",
  "data": {
    "action_id": "act_...",
    "code": "ABCD-EFGH-JKLM",
    "invitation": { "...": "..." }
  }
}
```

#### Errors

- `invite_generation_disabled`
- `missing_capability:registration.invites.generate`
- `not_staff`

---

### `/api/staffRevokeInvitationCode`

Capability: `registration.invites.revoke`. Revokes an **unused** code. Journal
action: `registration.invite_revoke`.

#### Request

```json
{
  "auth": { "username": "staff", "token": "..." },
  "code_id": "inv_...",
  "reason": "issued in error"
}
```

#### Response

```json
{
  "status": "ok",
  "data": {
    "action_id": "act_...",
    "invitation": { "...": "..." }
  }
}
```

#### Errors

- `invitation_code_not_found`
- `invitation_code_used`
- `invitation_code_already_revoked`
- `invites_disabled`
- `missing_capability:registration.invites.revoke`

#### Undo

- Undo generate deletes the code only if still unused.
- Undo revoke clears `revoked_at` only if still unused.
- Used codes are not reversible.
