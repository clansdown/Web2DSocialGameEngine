# Staff Console

The staff console is a hub activity visible only to accounts with a staff
record. It is visually distinct from gameplay cards. Server capability checks
are authoritative; the UI only hides tools.

## Routes

- `#/activity/staff`
- `#/activity/staff/accounts/<account_id>`
- `#/activity/staff/baronies/<barony_id>`
- `#/activity/staff/support/<ticket_id>`
- `#/activity/staff/audit/<action_id>`
- `#/activity/staff/unstick`
- `#/activity/staff/chats`

## Sections

- **Accounts** — search by username or character name; view characters,
  adult/social/support state; lock/revert name or portrait; remove a custom
  portrait; view identity history; ban/unban exact avatar SHA-256 hashes; grant
  or revoke a per-character monthly pass (arbitrary 31-day months or permanent)
  and inspect its append-only grant/revoke/restore history.
- **Baronies** — search, edit safe name/tags and optional custom adult text,
  toggle `accepts_minors` (rejected while a minor is a member).
- **Support** — queue, ticket detail, player-visible replies, internal notes,
  status changes, assignment, journal links, proactive ticket creation.
- **Audit** — filter the shared journal; inspect before/after state; preflight
  and undo reversible actions. No force-undo.
- **Unstick** — close active game session, set game phase, set march progress,
  cancel construction, adjust a fiefdom resource. Every operation is journaled
  and reversible with a snapshot.
- **Chats** — search any chat by name/id and open it. `chat.moderate_any` also
  enables the Chat screen's moderation-mode toggle.

## Grant / revoke

Staff membership is CLI-only in v1 (`staff_admin`). The console does not expose
grant/revoke, so a staff member cannot self-demote.
