# rate_limit_timeouts Table

Current active rate-limit timeout per account. Stored in `chat.db`.

## Schema

```sql
CREATE TABLE rate_limit_timeouts (
    account_id INTEGER PRIMARY KEY,
    level INTEGER NOT NULL DEFAULT 0,
    until INTEGER NOT NULL
);
```

## Notes

- `level` selects the escalation duration
  (`escalation_seconds`: 30s→5m→30m→2h→24h→7d).
- `until` is the unix time the timeout expires; `until > now` rejects writes with
  `rate_limited`.
- `level` decays to 0 after `decay_seconds` without a violation.
