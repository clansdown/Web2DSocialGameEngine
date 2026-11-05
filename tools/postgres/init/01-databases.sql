-- Creates the four independent Ravenest logical databases. Run against the
-- `postgres` maintenance database as a role with CREATEDB.
--
-- This file is idempotent and is used by the bundled compose `db-init` service
-- on every `up`, so it also works with an existing PostgreSQL data volume.
-- `current_user` is POSTGRES_USER in the bundled stack.
--
-- \gexec executes each generated CREATE DATABASE outside a single transaction,
-- which CREATE DATABASE requires.

SELECT format('CREATE DATABASE %I OWNER %I', 'ravenest_game', current_user)
WHERE NOT EXISTS (SELECT 1 FROM pg_database WHERE datname = 'ravenest_game')
\gexec

SELECT format('CREATE DATABASE %I OWNER %I', 'ravenest_chat', current_user)
WHERE NOT EXISTS (SELECT 1 FROM pg_database WHERE datname = 'ravenest_chat')
\gexec

SELECT format('CREATE DATABASE %I OWNER %I', 'ravenest_staff', current_user)
WHERE NOT EXISTS (SELECT 1 FROM pg_database WHERE datname = 'ravenest_staff')
\gexec

SELECT format('CREATE DATABASE %I OWNER %I', 'ravenest_invites', current_user)
WHERE NOT EXISTS (SELECT 1 FROM pg_database WHERE datname = 'ravenest_invites')
\gexec
