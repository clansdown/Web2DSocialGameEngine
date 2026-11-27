# Directory layout

This file documents the repository's source and runtime directories, with
particular attention to the two local PostgreSQL clusters. Runtime directories
are gitignored and must never be shared between development and testing.

## Source and content directories

| Directory | Purpose |
|---|---|
| `api/` | Per-endpoint API documentation. Every endpoint has a matching `.md` file. |
| `client/` | Vite + Svelte 5 client. `client/SimpleGame/` is an imported engine package and is never modified here. |
| `deploy/` | Generated production deployment compose files and helpers. |
| `docs/` | Design, protocol, database, authentication, and operations documentation. |
| `game/` | Game content root: `config/`, `images/`, `text/`, and mini-game data. Passed to the server with `--content`. |
| `server/` | C++23 API/game server, its build tree, and server-specific docs. |
| `tools/` | Development, test, deployment, and content-maintenance scripts. |

## Runtime directories

These directories are generated locally and ignored by git. They are not
interchangeable: the dev server and the smoke tests must never read from each
other's directory.

### `dev_run/` — manual development only

Used by `./run_server.sh` and `tools/dev_postgres.sh`.

| Path | Purpose |
|---|---|
| `dev_run/postgres/` | Dev PostgreSQL data directory (Docker bind mount). |
| `dev_run/dev_postgres.env` | Generated dev DB URLs, sourced only by `run_server.sh`. |
| `dev_run/uploads/` | Dev avatar uploads and other server-written uploads. |

The dev cluster listens on **127.0.0.1:5433** in the Docker project
`ravenest-dev-postgres`. Its env file contains the four `RAVENEST_*_DB_URL`
values, `RAVENEST_DEV_DB_PORT=5433`, and `RAVENEST_COOKIE_SECURE=0`; it does not
export the generic `RAVENEST_DB_HOST/PORT/USER/PASSWORD` variables that smoke
tests read.

### `test_run/` — automated smoke and agent tests only

Used by the smoke-test harnesses, `agent_test_server.sh`, and
`tools/test_postgres.sh`.

| Path | Purpose |
|---|---|
| `test_run/postgres/` | Isolated test PostgreSQL data directory (Docker bind mount). |
| `test_run/test_postgres.env` | Generated test DB connection variables. |
| `test_run/agent/` | `agent_test_server.sh` run directory; wiped and recreated per run. |
| `test_run/smoke_<port>/`, `test_run/chat_smoke_<port>/`, etc. | Per-test run directories, uploads, logs, and scratch content. |
| `test_run/replays_<port>/` | Recorded combat replay data when replay tests run. |

The test cluster listens on **127.0.0.1:5432** in the Docker project
`ravenest-test-postgres`. Smoke tests create and drop only their prefixed
scratch databases (`<prefix>_game`, `<prefix>_chat`, etc.). They never touch
`dev_run/`.

### Other generated directories

| Path | Purpose |
|---|---|
| `server/build/` | CMake build tree and server binaries. |
| `server/build-*/` | Alternate or sanitizer build trees. |
| `client/dist/` | Production client build output. |
| `server/translation/models/` | Local translation model cache. |
| `uploads/` | Fallback upload location when the server is run directly from the repository root. |

The legacy root `docker-compose.yml` uses the `ravenest_pgdata` Docker named
volume. It is not used by `dev_run/` or `test_run/`.

## Local PostgreSQL commands

```bash
# Development cluster (port 5433, data in dev_run/postgres)
bash tools/dev_postgres.sh up
bash tools/dev_postgres.sh status
bash tools/dev_postgres.sh down
bash tools/dev_postgres.sh reset --yes

# Test cluster (port 5432, data in test_run/postgres)
bash tools/test_postgres.sh up
bash tools/test_postgres.sh status
bash tools/test_postgres.sh down
bash tools/test_postgres.sh reset --yes

# Open psql inside either container (no host psql needed)
bash tools/dev_postgres.sh psql
bash tools/test_postgres.sh psql
```

The wrappers are invoked with `bash` above so they work even before their
executable bit is set; `chmod +x` them if you prefer `./tools/...`.

`run_server.sh` starts the dev cluster automatically. Smoke tests start the test
cluster automatically in local Docker mode; external/CI databases can be used
with `RAVENEST_TEST_ADMIN_URL` or `RAVENEST_TEST_USE_DOCKER=0`.

Smoke tests require a host `psql` client (`postgresql-client`) because several
test harnesses invoke `psql` directly. The PostgreSQL container includes
`psql`, but the host command is what the test scripts call.

`reset` removes the data directory and generated env file, but leaves uploads
and per-test run directories alone. The data directories may be owned by the
PostgreSQL container UID, so `reset` deletes them through a throwaway container
when necessary.

Overrides: `RAVENEST_DEV_DB_DATA_DIR`, `RAVENEST_DEV_DB_PORT`,
`RAVENEST_TEST_DB_DATA_DIR`, `RAVENEST_TEST_DB_PORT`, and
`RAVENEST_DB_USER`/`RAVENEST_DB_PASSWORD`. Changing the password after a data
directory has been initialized requires `reset` (or an `ALTER ROLE`).

## Isolation rules

- The dev server runs from `dev_run/`; smoke tests run from their own
  `test_run/...` directories.
- Dev PostgreSQL is on 5433; test PostgreSQL is on 5432.
- Dev and test use different Docker Compose projects, data directories, and env
  files.
- Never source `dev_run/dev_postgres.env` for smoke tests.
- Never point `run_server.sh` at `test_run/test_postgres.env`.
- The legacy root compose `postgres` service uses port 5432 and the `app`
  profile uses port 2290; do not run them alongside the test cluster or
  `run_server.sh`.
