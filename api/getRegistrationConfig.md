# POST /api/getRegistrationConfig

Public pre-authentication endpoint that tells the client whether account
creation requires an invitation code.

**Never returns the shared global code, individual codes, player limits, or
rate-limit settings.**

## Request

```json
{}
```

No authentication required.

## Response

### `off` mode

```json
{
  "status": "ok",
  "data": {
    "mode": "off",
    "invite_required": false,
    "players_can_generate": false
  }
}
```

### `global_code` mode

```json
{
  "status": "ok",
  "data": {
    "mode": "global_code",
    "invite_required": true,
    "players_can_generate": false
  }
}
```

### `invite_only` mode

```json
{
  "status": "ok",
  "data": {
    "mode": "invite_only",
    "invite_required": true,
    "players_can_generate": true
  }
}
```

## Fields

| Field | Type | Description |
|-------|------|-------------|
| mode | string | `off`, `global_code`, or `invite_only` |
| invite_required | boolean | True when `/api/createAccount` requires an invitation code |
| players_can_generate | boolean | Effective flag: only true in `invite_only` with player generation configured |

## Notes

- The create-account page hides the invitation field entirely when
  `invite_required` is false.
- The server remains authoritative: `off` mode ignores any submitted code.
- Config source: `game/config/registration.json`; see `docs/registration.md`.
