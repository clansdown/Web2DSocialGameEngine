# banned_avatar_hashes (`staff.db`)

Exact-match SHA-256 bans for custom avatar uploads. Bans are independent of the
avatar objects, so they survive deletion/replacement.

| Column | Type | Notes |
| --- | --- | --- |
| sha256 | TEXT PK | lowercase hex SHA-256 of the uploaded bytes |
| reason | TEXT | staff reason |
| banned_by_account_id | INTEGER | staff account |
| created_at | INTEGER | |
| revoked_at | INTEGER NULL | NULL while active |
| revoked_by_account_id | INTEGER NULL | |
| action_id | TEXT NULL | journal entry |

Upload completion checks the hash on the server loop before writing
`characters.avatar_custom_id`. A byte-identical re-upload is rejected with
`avatar_hash_banned`. Altered images evade an exact hash ban; this is a speed
bump, not a perceptual-hash guarantee. Undo revokes the active ban row.
