#!/usr/bin/env bash
# Shared scratch-PostgreSQL provisioning for smoke tests.
#
# Local Docker mode (default): starts/uses the isolated test cluster managed by
# tools/test_postgres.sh (test_run/postgres, host port 5432). The dev cluster in
# dev_run/ is never used.
#
# External mode is used automatically when any of these is set:
#   RAVENEST_TEST_ADMIN_URL   admin connection
#   RAVENEST_DB_HOST          non-local host
#   RAVENEST_DB_PORT          explicit port other than the test-cluster port
#   RAVENEST_TEST_USE_DOCKER=0
#
# Treat an explicit non-default RAVENEST_DB_PORT as an external database. Do
# not point it at the development cluster in dev_run/; the dev env file
# deliberately does not export RAVENEST_DB_PORT. In auto mode, the dev port
# (5433 by default) is rejected unless RAVENEST_TEST_USE_DOCKER=0 is set.
#
# Usage:
#   source "$ROOT/tools/postgres_test_env.sh"
#   postgres_test_env_init "ravenest_smoke_${PORT}"
#   trap postgres_test_env_cleanup EXIT
#
# Environment:
#   RAVENEST_TEST_ADMIN_URL   admin connection (external mode)
#   RAVENEST_DB_USER          app role (default: ravenest)
#   RAVENEST_DB_PASSWORD      app password (default: ravenest_dev)
#   RAVENEST_DB_HOST/PORT     endpoint (default: 127.0.0.1:5432)
#   RAVENEST_TEST_DB_PORT     Docker test-cluster host port (default: 5432)
#   RAVENEST_TEST_USE_DOCKER  1 = force Docker, 0 = external, auto = default
#   RAVENEST_DEV_DB_PORT      dev port rejected by the isolation guard (5433)

postgres_test_env_root() {
    printf '%s\n' "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
}

postgres_test_urlencode() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$1"
    else
        printf '%s' "$1"
    fi
}

postgres_test_psql_admin() {
    if [ -n "${RAVENEST_TEST_ADMIN_URL:-}" ]; then
        psql "$RAVENEST_TEST_ADMIN_URL" "$@"
        return
    fi
    PGCONNECT_TIMEOUT="${PGCONNECT_TIMEOUT:-5}" \
    PGPASSWORD="${RAVENEST_DB_PASSWORD:-ravenest_dev}" \
        psql \
            -h "${RAVENEST_DB_HOST:-127.0.0.1}" \
            -p "${RAVENEST_DB_PORT:-5432}" \
            -U "${RAVENEST_DB_USER:-ravenest}" \
            -d postgres \
            "$@"
}

postgres_test_env_use_docker() {
    case "${RAVENEST_TEST_USE_DOCKER:-auto}" in
        0|false|FALSE|no|NO) return 1 ;;
        1|true|TRUE|yes|YES) return 0 ;;
    esac

    if [ -n "${RAVENEST_TEST_ADMIN_URL:-}" ]; then
        return 1
    fi

    case "${RAVENEST_DB_HOST:-127.0.0.1}" in
        127.0.0.1|localhost|::1) ;;
        *) return 1 ;;
    esac

    if [ -n "${RAVENEST_DB_PORT:-}" ] && \
       [ "${RAVENEST_DB_PORT}" != "${RAVENEST_TEST_DB_PORT:-5432}" ]; then
        return 1
    fi

    return 0
}

postgres_test_env_reject_dev_port() {
    case "${RAVENEST_TEST_USE_DOCKER:-auto}" in
        0|false|FALSE|no|NO|1|true|TRUE|yes|YES) return 0 ;;
    esac

    case "${RAVENEST_DB_HOST:-127.0.0.1}" in
        127.0.0.1|localhost|::1) ;;
        *) return 0 ;;
    esac

    local dev_port="${RAVENEST_DEV_DB_PORT:-5433}"
    if [ "${RAVENEST_DB_PORT:-}" = "$dev_port" ]; then
        printf 'Error: refusing to run smoke tests against the dev PostgreSQL port %s.\n' "$dev_port" >&2
        printf 'Unset RAVENEST_DB_PORT, or set RAVENEST_TEST_USE_DOCKER=0 for an intentional external database.\n' >&2
        return 1
    fi
    return 0
}

postgres_test_env_ensure_cluster() {
    postgres_test_env_use_docker || return 0

    local root env_file
    root="$(postgres_test_env_root)"
    env_file="$root/test_run/test_postgres.env"

    printf 'Starting isolated test PostgreSQL cluster...\n' >&2
    if ! bash "$root/tools/test_postgres.sh" up; then
        return 1
    fi

    if [ ! -f "$env_file" ]; then
        printf 'Error: test PostgreSQL env file was not created: %s\n' "$env_file" >&2
        return 1
    fi

    # shellcheck disable=SC1090
    . "$env_file"
    export RAVENEST_DB_HOST RAVENEST_DB_PORT RAVENEST_DB_USER RAVENEST_DB_PASSWORD

    # Docker mode always uses the generated connection details, even if an
    # admin URL was also present in the environment.
    unset RAVENEST_TEST_ADMIN_URL
}

postgres_test_env_drop() {
    local prefix="${RAVENEST_TEST_DB_PREFIX:-}"
    [ -n "$prefix" ] || return 0
    local suffix
    for suffix in game chat staff invites; do
        postgres_test_psql_admin -v ON_ERROR_STOP=1 \
            -c "DROP DATABASE IF EXISTS \"${prefix}_${suffix}\"" >/dev/null 2>&1 || true
    done
}

postgres_test_env_cleanup() {
    if [ "${RAVENEST_KEEP_TEST_DB:-0}" = "1" ]; then
        echo "Keeping scratch databases with prefix ${RAVENEST_TEST_DB_PREFIX:-}" >&2
        return 0
    fi
    postgres_test_env_drop
}

postgres_test_env_init() {
    local prefix="$1"
    if ! command -v psql >/dev/null 2>&1; then
        echo "Error: psql is required (install postgresql-client)" >&2
        exit 2
    fi

    postgres_test_env_reject_dev_port || exit 2
    postgres_test_env_ensure_cluster || exit 2

    : "${RAVENEST_DB_USER:=ravenest}"
    : "${RAVENEST_DB_PASSWORD:=ravenest_dev}"
    : "${RAVENEST_DB_HOST:=127.0.0.1}"
    : "${RAVENEST_DB_PORT:=5432}"
    export RAVENEST_DB_USER RAVENEST_DB_PASSWORD RAVENEST_DB_HOST RAVENEST_DB_PORT
    export RAVENEST_TEST_DB_PREFIX="$prefix"

    local user_encoded password_encoded
    user_encoded="$(postgres_test_urlencode "$RAVENEST_DB_USER")"
    password_encoded="$(postgres_test_urlencode "$RAVENEST_DB_PASSWORD")"

    postgres_test_env_drop

    local suffix
    for suffix in game chat staff invites; do
        postgres_test_psql_admin -v ON_ERROR_STOP=1 \
            -c "CREATE DATABASE \"${prefix}_${suffix}\" OWNER \"${RAVENEST_DB_USER}\"" >/dev/null
    done

    export RAVENEST_GAME_DB_URL="postgresql://${user_encoded}:${password_encoded}@${RAVENEST_DB_HOST}:${RAVENEST_DB_PORT}/${prefix}_game"
    export RAVENEST_CHAT_DB_URL="postgresql://${user_encoded}:${password_encoded}@${RAVENEST_DB_HOST}:${RAVENEST_DB_PORT}/${prefix}_chat"
    export RAVENEST_STAFF_DB_URL="postgresql://${user_encoded}:${password_encoded}@${RAVENEST_DB_HOST}:${RAVENEST_DB_PORT}/${prefix}_staff"
    export RAVENEST_INVITES_DB_URL="postgresql://${user_encoded}:${password_encoded}@${RAVENEST_DB_HOST}:${RAVENEST_DB_PORT}/${prefix}_invites"
}
