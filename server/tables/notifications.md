# notifications (`staff.db`)

Account-level system notifications. See `docs/notifications.md`.

| Column | Type | Notes |
| --- | --- | --- |
| id | TEXT PK | `ntf_…` |
| account_id | INTEGER | recipient |
| character_id | INTEGER NULL | affected character, when applicable |
| kind | TEXT | moderation_action/moderation_reversal/social_restriction/support_lock/support_reply/support_ticket/staff_notice |
| text_id | TEXT | text-system key |
| text_args | TEXT | JSON token substitutions |
| ticket_id | TEXT NULL | support link |
| action_id | TEXT NULL | journal link |
| route | TEXT NULL | client hash route |
| created_at | INTEGER | |
| read_at | INTEGER NULL | NULL until read |

Notifications persist until read and are readable while socially restricted or
support-locked. They never contain custom avatar keys or support free-text
bodies.
