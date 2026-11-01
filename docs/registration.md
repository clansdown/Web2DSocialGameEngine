# Registration Gates and Invitation Codes

Canonical config: `game/config/registration.json`  
Canonical table: `server/tables/invitation_codes.md`

## Modes

| Mode | Account creation | Individual codes | Player generation |
|------|------------------|------------------|-------------------|
| `off` | Anyone. No code input; any submitted code is ignored. | Ignored. | Disabled. |
| `global_code` | The configured shared code is required. | Still valid. | Disabled. |
| `invite_only` | A single-use individual code is required. | Required. | Allowed only when configured and within limits. |

`ravenest_invites` is opened lazily. `off` mode never opens it. Mode changes do
not invalidate existing individual codes.

## Bootstrapping staff

Staff are normal accounts. In `invite_only` mode there is no way to create the
first account without a code, so use the CLI against the test/ops database:

```bash
./invite_admin create --count 1
```

Then create the staff account and grant `staff_admin`. The server can also be
temporarily started in `off`/`global_code` mode for the first account, then
switched back to `invite_only`.

## Code format

- 12 uppercase Crockford-style characters: `0123456789ABCDEFGHJKMNPQRSTVWXYZ`.
- Stored canonical without dashes; displayed as `XXXX-XXXX-XXXX`.
- Input is forgiving: dashes/spaces are ignored, and `O` → `0`, `I`/`L` → `1`.
- Plaintext storage, no expiry.

## Single-use guarantee

`ravenest_game` `users.invitation_code_id` is the authoritative redemption ledger and
has a partial UNIQUE index. Two concurrent account creations with the same code can
both pass the invite-row lookup, but only one can insert the user; the loser
gets `invalid_invitation_code`. The `ravenest_invites` row is marked used only after
the game transaction commits, so a failed account creation does not burn the
code. If marking the row used fails, the game ledger still blocks reuse.

## Staff actions

- `registration.invites.view` — list codes (logged as `read.invitation_codes`).
- `registration.invites.generate` — generate a code (invite_only only).
- `registration.invites.revoke` — revoke an unused code.

Generation/revocation are journaled as `registration.invite_generate` and
`registration.invite_revoke`. **The plaintext code is never stored in the staff
journal**, because `ravenest_staff` is permanent while `ravenest_invites` is temporary.
Undo deletes an unused generated code, or clears revocation on an unused code.

## Retiring the feature

1. Set `mode` to `off`.
2. Stop the server.
3. Drop the `ravenest_invites` database.
4. Optionally `UPDATE users SET invitation_code_id = NULL;` in `ravenest_game`.

## Rate limiting

Account creation is rate-limited per network before any validation:

- IPv4: exact address.
- IPv6: masked to `ipv6_prefix_bits` (default `/64`).
- IPv4-mapped IPv6 is treated as IPv4.
- Prefer `x-real-ip`, else the first `x-forwarded-for` address, else one
  `unknown` bucket.
- Bounded LRU: expired entries are purged first, then least-recently-used
  entries are evicted when `max_entries` is reached.
