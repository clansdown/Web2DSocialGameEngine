# Registration Gates and Invitation Codes

Canonical config: `game/config/registration.json`  
Canonical table: `server/tables/invitation_codes.md`

## Modes

| Mode | Account creation | Individual codes | Player generation |
|------|------------------|------------------|-------------------|
| `off` | Anyone. No code input; any submitted code is ignored. | Ignored. | Disabled. |
| `global_code` | The configured shared code is required. | Still valid. | Disabled. |
| `invite_only` | A single-use individual code is required. | Required. | Allowed only when configured and within limits. |

`invites.db` is opened lazily. `off` mode never creates or opens it. Mode
changes do not invalidate existing individual codes.

## Config

```json
{
  "mode": "off",
  "global_code": "",
  "players_can_generate": false,
  "player_daily_limit": 0,
  "player_lifetime_limit": 0,
  "create_account_rate_limit": {
    "enabled": true,
    "window_seconds": 3600,
    "max_attempts_per_network": 300,
    "ipv6_prefix_bits": 64,
    "max_entries": 10000,
    "prune_interval_seconds": 60
  }
}
```

- `players_can_generate` only has effect in `invite_only`.
- Either player limit being `0` disables player generation.
- Daily limits use UTC calendar days. Revoked codes still count toward the
  player's generation limits.
- `max_entries` is a memory ceiling, not a preallocation, and has no upper
  bound validation.
- Rate-limit counters are in-memory only and reset on restart.

## Bootstrapping staff

Staff are normal accounts. In `invite_only` mode there is no way to create the
first account without a code, so the intended bootstrap is:

1. Start in `off` or `global_code` mode.
2. Create the staff account(s).
3. Grant staff with `staff_admin`.
4. Switch to `invite_only`; staff can now generate codes.

## Code format

- 12 uppercase Crockford-style characters: `0123456789ABCDEFGHJKMNPQRSTVWXYZ`.
- Stored canonical without dashes; displayed as `XXXX-XXXX-XXXX`.
- Input is forgiving: dashes/spaces are ignored, and `O` → `0`, `I`/`L` → `1`.
- Plaintext storage, no expiry.

## Single-use guarantee

`game.db` `users.invitation_code_id` is the authoritative redemption ledger and
has a UNIQUE index. Two concurrent account creations with the same code can
both pass the invite-row lookup, but only one can insert the user; the loser
gets `invalid_invitation_code`. The `invites.db` row is marked used only after
the game transaction commits, so a failed account creation does not burn the
code. If marking the row used fails, the game ledger still blocks reuse.

## Staff actions

- `registration.invites.view` — list codes (logged as `read.invitation_codes`).
- `registration.invites.generate` — generate a code (invite_only only).
- `registration.invites.revoke` — revoke an unused code.

Generation/revocation are journaled as `registration.invite_generate` and
`registration.invite_revoke`. **The plaintext code is never stored in the staff
journal**, because `staff.db` is permanent while `invites.db` is temporary.
Undo deletes an unused generated code, or clears revocation on an unused code.

## Retiring the feature

1. Set `mode` to `off`.
2. Stop the server.
3. Delete `invites.db`.
4. Optionally `UPDATE users SET invitation_code_id = NULL;` in `game.db`.

## Rate limiting

Account creation is rate-limited per network before any validation:

- IPv4: exact address.
- IPv6: masked to `ipv6_prefix_bits` (default `/64`).
- IPv4-mapped IPv6 is treated as IPv4.
- Prefer `x-real-ip`, else the first `x-forwarded-for` address, else one
  `unknown` bucket.
- Bounded LRU: expired entries are purged first, then least-recently-used
  entries are evicted when `max_entries` is reached.
