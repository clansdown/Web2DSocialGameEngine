# PostgreSQL operations

## Local cluster

```bash
export RAVENEST_DB_PASSWORD=ravenest_dev
docker compose up -d postgres
./tools/init_postgres.sh
```

`tools/init_postgres.sh` creates the `ravenest` role and the four databases. It is idempotent.

Set the URLs (see `ravenest.env.example`):

```bash
export RAVENEST_GAME_DB_URL=postgresql://ravenest:ravenest_dev@127.0.0.1:5432/ravenest_game
export RAVENEST_CHAT_DB_URL=postgresql://ravenest:ravenest_dev@127.0.0.1:5432/ravenest_chat
export RAVENEST_STAFF_DB_URL=postgresql://ravenest:ravenest_dev@127.0.0.1:5432/ravenest_staff
export RAVENEST_INVITES_DB_URL=postgresql://ravenest:ravenest_dev@127.0.0.1:5432/ravenest_invites
```

The server applies the baseline schema on startup. `--init-db`, `--create-tables`, and `--ensure-indexes` are still accepted for operations scripts; all three are idempotent schema applications.

## Invitations (pre-alpha)

`game/config/registration.json` is set to `invite_only`. Generate codes before opening the client:

```bash
./invite_admin create --count 20
./invite_admin list
./invite_admin revoke <invitation-id>
```

`invite_admin` reads `RAVENEST_INVITES_DB_URL` (default `dbname=ravenest_invites`).

## Backups

```bash
RAVENEST_DB_PASSWORD=... ./tools/backup_postgres.sh backups
```

Each database is dumped with `pg_dump -Fc`. Restore one database with:

```bash
pg_restore --clean --if-exists -d ravenest_game backups/ravenest_game-<stamp>.dump
```

Test a restore into a scratch database before relying on a backup.

## Health and tuning

- Per-connection timeouts: `statement_timeout=5s`, `lock_timeout=2s`, `idle_in_transaction_session_timeout=10s`.
- Slow queries: `log_min_duration_statement=250ms` (set in `docker-compose.yml`).
- Inspect active sessions:

```sql
SELECT pid, usename, application_name, state, wait_event_type, query
FROM pg_stat_activity
WHERE datname LIKE 'ravenest_%';
```

- Connection count is small by design: 8 HTTP workers × 4 databases + combat/CLI. `max_connections=100` is ample.
- No pgbouncer in Phase 1. Add it only when the process count grows.

## Schema changes

Edit `server/db/BaselineSchema.hpp` for the pre-alpha baseline. Once a deployment has data, add a numbered migration instead of editing an applied baseline, and record it in `schema_migrations`.

## Failure behavior

- A database connection failure fails the current request with an error; the next request opens a fresh thread-local connection.
- `invite_only` fails closed if `ravenest_invites` is unreachable.
- The server does not fall back to SQLite.
