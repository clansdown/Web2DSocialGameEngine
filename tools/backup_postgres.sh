#!/usr/bin/env bash
# Backs up all four Ravenest databases with pg_dump -Fc (custom format).
#
# Usage:
#   RAVENEST_DB_PASSWORD=... ./tools/backup_postgres.sh [output-dir]
#
# Environment:
#   PGHOST, PGPORT, PGUSER, PGPASSWORD
#   or RAVENEST_DB_URL (used as a base; the database name is replaced)
#
# Restore:
#   pg_restore --clean --if-exists -d <database> <backup-file>
set -euo pipefail

out_dir="${1:-backups}"
mkdir -p "$out_dir"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"

if ! command -v pg_dump >/dev/null 2>&1; then
  echo "pg_dump not found (install postgresql-client)" >&2
  exit 1
fi

for database in ravenest_game ravenest_chat ravenest_staff ravenest_invites; do
  target="${out_dir}/${database}-${stamp}.dump"
  echo "Backing up ${database} -> ${target}"
  pg_dump --format=custom --no-owner --no-privileges \
    --dbname="$database" --file="$target"
done

echo "Backup complete: ${out_dir}"
