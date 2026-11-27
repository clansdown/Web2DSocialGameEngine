#!/usr/bin/env bash
# Isolated smoke-test PostgreSQL cluster.
#
# Data lives in test_run/postgres. The cluster uses host port 5432, matching the
# existing smoke-test defaults, and is completely separate from dev_run/.
#
# Usage: tools/test_postgres.sh <up|down|reset|status|psql|logs>

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/_postgres_common.sh
source "$SCRIPT_DIR/_postgres_common.sh"

POSTGRES_PROFILE="test"
POSTGRES_PROJECT="ravenest-test-postgres"
POSTGRES_RUNTIME_ROOT="$POSTGRES_COMMON_ROOT/test_run"
POSTGRES_DATA_DIR="${RAVENEST_TEST_DB_DATA_DIR:-$POSTGRES_RUNTIME_ROOT/postgres}"
POSTGRES_HOST_PORT="${RAVENEST_TEST_DB_PORT:-5432}"
POSTGRES_ENV_FILE="${RAVENEST_TEST_DB_ENV_FILE:-$POSTGRES_RUNTIME_ROOT/test_postgres.env}"
POSTGRES_ENV_MODE="test"

postgres_common_main "$@"
