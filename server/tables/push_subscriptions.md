# push_subscriptions Table

Web Push subscriptions, keyed on **account** (all characters of an account).
Stored in `chat.db`.

## Schema

```sql
CREATE TABLE push_subscriptions (
    id TEXT PRIMARY KEY,
    account_id INTEGER NOT NULL,
    endpoint TEXT NOT NULL,
    p256dh TEXT NOT NULL,
    auth TEXT NOT NULL,
    vapid_key_id TEXT,
    created_at INTEGER NOT NULL,
    last_success_at INTEGER,
    fail_count INTEGER NOT NULL DEFAULT 0,
    UNIQUE (account_id, endpoint)
);
```

## Notes

- `p256dh`/`auth` are the base64url subscription keys (RFC 8291).
- `vapid_key_id` records which keyring key signed the subscription, so key
  rotation is non-breaking.
- `send` returning 404/410 deletes the row; `fail_count` tracks retryables.
