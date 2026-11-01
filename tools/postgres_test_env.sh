#!/usr/bin/env bash
# Shared scratch-PostgreSQL provisioning for smoke tests.
#
# Usage:
#   source "$ROOT/tools/postgres_test_env.sh"
#   postgres_test_env_init "ravenest_smoke_${PORT}"
#   trap postgres_test_env_cleanup EXIT
#
# Environment:
#   RAVENEST_TEST_ADMIN_URL   admin connection (default: psql -d postgres)
#   RAVENEST_DB_USER          app role (default: ravenest)
#   RAVENEST_DB_PASSWORD      app password (default: ravenest_dev)
#   RAVENEST_DB_HOST/PORT     endpoint (default: 127.0.0.1:5432)

postgres_test_psql_admin() {
    if [ -n "${RAVENEST_TEST_ADMIN_URL:-}" ]; then
        psql "$RAVENEST_TEST_ADMIN_URL" "$@"
    else
        psql -d postgres "$@"
    fi
}

postgres_test_env_drop() {
    local prefix="${RAVENEST_TEST_DB_PREFIX:-}"
    [ -n "$prefix" ] || return 0
    for suffix in game chat staff invites; do
        postgres_test_psql_admin -v ON_ERROR_STOP=1 \
            -c "DROP DATABASE IF EXISTS \"${prefix}_${suffix}\"" >/dev/null 2>&1 || true
    done
}

postgres_test_env_cleanup() {
    if [ "${RAVENEST_KEEP_TEST_DB:-0}" = "1" ]; then
        echo "Keeping scratch databases with prefix ${RAVENEST_TEST_DB_PREFIX}" >&2
        return 0
    fi
    postgres_test_env_drop
}

postgres_test_env_init() {
    local prefix="$1"
    if ! command -v psql >/dev/null 2>&1; then
        echo "Error: psql is required (install postgresql-client)" >&2
        return 1
    fi

    : "${RAVENEST_DB_USER:=ravenest}"
    : "${RAVENEST_DB_PASSWORD:=ravenest_dev}"
    : "${RAVENEST_DB_HOST:=127.0.0.1}"
    : "${RAVENEST_DB_PORT:=5432}"
    export RAVENEST_DB_USER RAVENEST_DB_PASSWORD RAVENEST_DB_HOST RAVENEST_DB_PORT
    export RAVENEST_TEST_DB_PREFIX="$prefix"

    postgres_test_env_drop
    for suffix in game chat staff invites; do
        postgres_test_psql_admin -v ON_ERROR_STOP=1 \
            -c "CREATE DATABASE \"${prefix}_${suffix}\" OWNER \"${RAVENEST_DB_USER}\"" >/dev/null
    done

    export RAVENEST_GAME_DB_URL="postgresql://${RAVENEST_DB_USER}:${RAVENEST_DB_PASSWORD}@${RAVENEST_DB_HOST}:${RAVENEST_DB_PORT}/${prefix}_game"
    export RAVENEST_CHAT_DB_URL="postgresql://${RAVENEST_DB_USER}:${RAVENEST_DB_PASSWORD}@${RAVENEST_DB_HOST}:${RAVENEST_DB_PORT}/${prefix}_chat"
    export RAVENEST_STAFF_DB_URL="postgresql://${RAVENEST_DB_USER}:${RAVENEST_DB_PASSWORD}@${RAVENEST_DB_HOST}:${RAVENEST_DB_PORT}/${prefix}_staff"
    export RAVENEST_INVITES_DB_URL="postgresql://${RAVENEST_DB_USER}:${RAVENEST_DB_PASSWORD}@${RAVENEST_DB_HOST}:${RAVENEST_DB_PORT}/${prefix}_invites"
}
