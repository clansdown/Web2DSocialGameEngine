# rate_limit_violations Table

Per-account request/violation log used for flood control. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE rate_limit_violations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    kind TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
```

## Notes

- `kind='request'` records an accepted `sendChatMessage`; `kind='violation'`
  records a breach.
- Request rows older than `rate_limit.decay_seconds` are pruned.
- Counts in the last second/minute/5 minutes are compared to the configured
  limits.
