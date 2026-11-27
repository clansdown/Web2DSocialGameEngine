#!/usr/bin/env bash
# Local development PostgreSQL cluster.
#
# Data lives in dev_run/postgres. The cluster uses host port 5433 so it can
# never collide with the isolated smoke-test cluster on 5432.
#
# Usage: tools/dev_postgres.sh <up|down|reset|status|psql|logs>

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/_postgres_common.sh
source "$SCRIPT_DIR/_postgres_common.sh"

POSTGRES_PROFILE="dev"
POSTGRES_PROJECT="ravenest-dev-postgres"
POSTGRES_RUNTIME_ROOT="$POSTGRES_COMMON_ROOT/dev_run"
POSTGRES_DATA_DIR="${RAVENEST_DEV_DB_DATA_DIR:-$POSTGRES_RUNTIME_ROOT/postgres}"
POSTGRES_HOST_PORT="${RAVENEST_DEV_DB_PORT:-5433}"
POSTGRES_ENV_FILE="${RAVENEST_DEV_DB_ENV_FILE:-$POSTGRES_RUNTIME_ROOT/dev_postgres.env}"
POSTGRES_ENV_MODE="dev"

postgres_common_main "$@"
