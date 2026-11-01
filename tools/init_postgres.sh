#!/usr/bin/env bash
# Creates the Ravenest PostgreSQL role and four logical databases.
#
# Idempotent: safe to run against an existing cluster. Requires psql and a
# superuser connection (PGUSER, default: postgres).
#
# Environment:
#   PGHOST, PGPORT, PGUSER, PGPASSWORD   libpq admin connection
#   RAVENEST_DB_NAME                     role name (default: ravenest)
#   RAVENEST_DB_PASSWORD                 role password (required)
#   RAVENEST_DB_HOST                     host used in printed URLs (default: 127.0.0.1)
#   RAVENEST_DB_PORT                     port used in printed URLs (default: 5432)
set -euo pipefail

role="${RAVENEST_DB_NAME:-ravenest}"
password="${RAVENEST_DB_PASSWORD:-}"
if [[ -z "$password" ]]; then
  echo "RAVENEST_DB_PASSWORD is required" >&2
  exit 1
fi

host="${RAVENEST_DB_HOST:-127.0.0.1}"
port="${RAVENEST_DB_PORT:-5432}"
admin_user="${PGUSER:-postgres}"

if ! command -v psql >/dev/null 2>&1; then
  echo "psql not found (install postgresql-client)" >&2
  exit 1
fi

echo "Ensuring role '${role}'..."
psql -v ON_ERROR_STOP=1 --username "$admin_user" --dbname postgres \
  --set role_name="$role" --set role_password="$password" <<'SQL'
SELECT format('CREATE ROLE %I LOGIN PASSWORD %L', :'role_name', :'role_password')
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = :'role_name')\gexec
SELECT format('ALTER ROLE %I WITH LOGIN PASSWORD %L', :'role_name', :'role_password')\gexec
SQL

for database in ravenest_game ravenest_chat ravenest_staff ravenest_invites; do
  echo "Ensuring database '${database}'..."
  psql -v ON_ERROR_STOP=1 --username "$admin_user" --dbname postgres \
    --set db_name="$database" --set role_name="$role" <<'SQL'
SELECT format('CREATE DATABASE %I OWNER %I', :'db_name', :'role_name')
WHERE NOT EXISTS (SELECT 1 FROM pg_database WHERE datname = :'db_name')\gexec
SQL
done

encoded_password="$password"
if command -v python3 >/dev/null 2>&1; then
  encoded_password="$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$password")"
fi
echo
echo "Connection URLs:"
for database in ravenest_game ravenest_chat ravenest_staff ravenest_invites; do
  echo "  postgresql://${role}:${encoded_password}@${host}:${port}/${database}"
done
echo
echo "Export the matching RAVENEST_{GAME,CHAT,STAFF,INVITES}_DB_URL variables."
