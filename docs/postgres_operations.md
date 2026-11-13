# PostgreSQL operations

## Local cluster

```bash
export RAVENEST_DB_PASSWORD=ravenest_dev
docker compose up -d postgres
```

The compose `postgres` service creates `ravenest_game` via `POSTGRES_DB`;
`tools/postgres/init/01-databases.sql` also creates all four databases
idempotently. In the local compose service the script is mounted under
`/docker-entrypoint-initdb.d`, so it only runs on the first boot of an empty
data volume. Production bundled mode runs the same SQL through the `db-init`
one-shot service on every `up`.

For a host-managed or pre-existing cluster instead of the compose service:

```bash
export RAVENEST_DB_PASSWORD=ravenest_dev
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

Those are correct for `./run_server.sh` on the host. The local compose `app`
profile uses the service name instead:

```bash
RAVENEST_DB_PASSWORD=ravenest_dev docker compose --profile app up -d --build
```

Inside that network the DB URLs are
`postgresql://ravenest:<password>@postgres:5432/ravenest_*`.

## Production topologies

`tools/install.sh` supports two database modes.

### Bundled (`SERVER_DB_MODE=bundled`, default)

Docker Compose runs PostgreSQL next to the app:

- `deploy/docker-compose.bundled.yml` starts `postgres:16-alpine`;
- the `db-init` one-shot service creates the four databases idempotently;
- no PostgreSQL port is published to the host;
- data lives in the `ravenest_pgdata` Docker volume;
- generated URLs use the compose service name `postgres`.

Required configuration:

```bash
SERVER_DEPLOY_MODE=docker
SERVER_DB_MODE=bundled
SERVER_DB_USER=ravenest
SERVER_DB_PASSWORD=...       # generate once: openssl rand -hex 24
```

`SERVER_DB_PASSWORD` must be URL-safe because it is used both as the PostgreSQL
password and inside the libpq URLs. Switching to bundled does not import an
existing external database; dump/restore it first.

Back up the bundled stack with:

```bash
cd "$SERVER_PATH"
docker compose --env-file compose.env \
  -f docker-compose.yml -f docker-compose.bundled.yml \
  exec -T postgres pg_dumpall -U ravenest > "backup-$(date -u +%Y%m%dT%H%M%SZ).sql"
```

Restore into an empty stack:

```bash
docker compose --env-file compose.env \
  -f docker-compose.yml -f docker-compose.bundled.yml \
  exec -T postgres psql -U ravenest -d postgres < backup.sql
```

### External (`SERVER_DB_MODE=external`)

The app runs in compose or on the host, and PostgreSQL is managed separately.
Create the four databases and a role with access, then supply all four
`SERVER_*_DB_URL` values. Credentials are embedded in the URLs; percent-encode
reserved characters. Managed providers usually need `?sslmode=require`.

With a Docker app, a DB URL cannot use `127.0.0.1`; that is the app container
itself. Use the host's LAN address, a managed hostname, or a compose service
name. With a systemd app, `127.0.0.1` is correct.

`systemd` + `bundled` is rejected. Run PostgreSQL separately and use
`SERVER_DB_MODE=external` if the app is not containerized.

The server applies the baseline schema on startup. `--init-db`, `--create-tables`, and `--ensure-indexes` are still accepted for operations scripts; all three are idempotent schema applications.

## Invitations (pre-alpha)

`game/config/registration.json` is set to `invite_only`. Generate codes before opening the client:

```bash
./tools/invite_admin.sh create --count 20
./tools/invite_admin.sh list
./tools/invite_admin.sh revoke <invitation-id>
```

On a Docker deployment the installed wrapper runs `/app/invite_admin` inside the
running `ravenest` container, which already has `RAVENEST_INVITES_DB_URL`.

## Backups

For a host-managed/external cluster:

```bash
RAVENEST_DB_PASSWORD=... ./tools/backup_postgres.sh backups
```

For the bundled compose stack, use the `pg_dumpall` command in the
“Bundled” production topology above. It dumps all four databases in one file.
Restoring a bundled dump into an empty stack uses `psql -d postgres`.

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
