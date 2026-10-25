# support.* tables (`staff.db`)

Account-level, text-only support tickets. See `docs/support_tickets.md`.

## `support_tickets`

| Column | Type | Notes |
| --- | --- | --- |
| id | TEXT PK | `tkt_…` |
| account_id | INTEGER | ticket owner (`users.id`) |
| character_id | INTEGER NULL | optional context |
| category | TEXT | appeal/report/technical/account/other |
| subject | TEXT | 1–160 characters |
| status | TEXT | open/awaiting_staff/awaiting_player/resolved/closed |
| priority | TEXT | normal/high/urgent |
| assigned_account_id | INTEGER NULL | staff assignee |
| player_last_read_at | INTEGER | per-side read cursor |
| staff_last_read_at | INTEGER | per-side read cursor |
| created_at / updated_at | INTEGER | |
| closed_at | INTEGER NULL | set when closed |

## `support_ticket_messages`

| Column | Type | Notes |
| --- | --- | --- |
| id | TEXT PK | `msg_…` |
| ticket_id | TEXT | |
| author_kind | TEXT | player/staff/system |
| author_account_id | INTEGER NULL | |
| author_character_id | INTEGER NULL | |
| body | TEXT | free text (private support channel) |
| is_internal | INTEGER | staff-only note; never returned to players |
| created_at | INTEGER | |

## `support_ticket_action_links`

`(ticket_id, action_id)` primary key; links the ticket to staff journal actions
(e.g. the moderation action under appeal). `created_by_account_id` records the
staff member who made the link.
