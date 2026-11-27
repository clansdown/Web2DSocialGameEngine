#!/usr/bin/env bash
#
# Shared lifecycle for the isolated local dev and smoke-test PostgreSQL
# clusters. Sourced by tools/dev_postgres.sh and tools/test_postgres.sh.
#
# The wrappers set:
#   POSTGRES_PROFILE       dev | test
#   POSTGRES_PROJECT       Docker Compose project name
#   POSTGRES_RUNTIME_ROOT  dev_run or test_run, used to validate reset paths
#   POSTGRES_DATA_DIR      host bind-mount directory
#   POSTGRES_HOST_PORT     127.0.0.1 host port
#   POSTGRES_ENV_FILE      generated connection env file
#   POSTGRES_ENV_MODE      dev | test
#
# This file deliberately contains no profile defaults so the two clusters cannot
# accidentally share a project, data directory, port, or env file.

set -uo pipefail

POSTGRES_COMMON_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
POSTGRES_COMMON_COMPOSE_FILE="$POSTGRES_COMMON_ROOT/tools/local_postgres.compose.yml"
POSTGRES_COMMON_INIT_SQL="$POSTGRES_COMMON_ROOT/tools/postgres/init/01-databases.sql"
POSTGRES_COMMON_INIT_DIR="$POSTGRES_COMMON_ROOT/tools/postgres/init"
POSTGRES_COMMON_USER="${RAVENEST_DB_USER:-ravenest}"
POSTGRES_COMMON_PASSWORD="${RAVENEST_DB_PASSWORD:-ravenest_dev}"

postgres_common_error() {
    printf 'error: %s\n' "$*" >&2
}

postgres_common_die() {
    postgres_common_error "$*"
    exit 2
}

postgres_common_usage() {
    cat <<EOF
Usage: tools/${POSTGRES_PROFILE}_postgres.sh <command> [options]

Commands:
  up                  Start and initialize the ${POSTGRES_PROFILE} cluster
  down                Stop the cluster, keeping its data
  reset [--yes]       Stop the cluster and delete its data directory
  status              Show container, port, data directory, and databases
  psql [database]     Open psql in the cluster (default: ravenest_game)
  logs                Show recent PostgreSQL logs

Environment:
  dev:  RAVENEST_DEV_DB_DATA_DIR, RAVENEST_DEV_DB_PORT
  test: RAVENEST_TEST_DB_DATA_DIR, RAVENEST_TEST_DB_PORT
  both: RAVENEST_DB_USER, RAVENEST_DB_PASSWORD
EOF
}

postgres_common_urlencode() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$1"
    else
        printf '%s' "$1"
    fi
}

postgres_common_resolve_paths() {
    case "$POSTGRES_DATA_DIR" in
        /*) ;;
        *) POSTGRES_DATA_DIR="$POSTGRES_COMMON_ROOT/$POSTGRES_DATA_DIR" ;;
    esac
    case "$POSTGRES_ENV_FILE" in
        /*) ;;
        *) POSTGRES_ENV_FILE="$POSTGRES_COMMON_ROOT/$POSTGRES_ENV_FILE" ;;
    esac
    case "$POSTGRES_RUNTIME_ROOT" in
        /*) ;;
        *) POSTGRES_RUNTIME_ROOT="$POSTGRES_COMMON_ROOT/$POSTGRES_RUNTIME_ROOT" ;;
    esac

    case "$POSTGRES_HOST_PORT" in
        ''|*[!0-9]*)
            postgres_common_die "invalid ${POSTGRES_PROFILE} PostgreSQL port: $POSTGRES_HOST_PORT"
            ;;
    esac
    if [ "$POSTGRES_HOST_PORT" -lt 1 ] || [ "$POSTGRES_HOST_PORT" -gt 65535 ]; then
        postgres_common_die "invalid ${POSTGRES_PROFILE} PostgreSQL port: $POSTGRES_HOST_PORT"
    fi

    if [ "$POSTGRES_PROFILE" = "dev" ] && \
       [ "$POSTGRES_HOST_PORT" = "${RAVENEST_TEST_DB_PORT:-5432}" ]; then
        postgres_common_die "dev and test PostgreSQL ports must differ (both are $POSTGRES_HOST_PORT)"
    fi
    if [ "$POSTGRES_PROFILE" = "test" ] && \
       [ "$POSTGRES_HOST_PORT" = "${RAVENEST_DEV_DB_PORT:-5433}" ]; then
        postgres_common_die "dev and test PostgreSQL ports must differ (both are $POSTGRES_HOST_PORT)"
    fi

    local other_data_dir
    if [ "$POSTGRES_PROFILE" = "dev" ]; then
        other_data_dir="${RAVENEST_TEST_DB_DATA_DIR:-$POSTGRES_COMMON_ROOT/test_run/postgres}"
    else
        other_data_dir="${RAVENEST_DEV_DB_DATA_DIR:-$POSTGRES_COMMON_ROOT/dev_run/postgres}"
    fi
    case "$other_data_dir" in
        /*) ;;
        *) other_data_dir="$POSTGRES_COMMON_ROOT/$other_data_dir" ;;
    esac
    if [ "$POSTGRES_DATA_DIR" = "$other_data_dir" ]; then
        postgres_common_die "dev and test PostgreSQL data directories must differ: $POSTGRES_DATA_DIR"
    fi

    export RAVENEST_PG_DATA_DIR="$POSTGRES_DATA_DIR"
    export RAVENEST_PG_HOST_PORT="$POSTGRES_HOST_PORT"
    export RAVENEST_PG_INIT_DIR="$POSTGRES_COMMON_INIT_DIR"
    export RAVENEST_PG_USER="$POSTGRES_COMMON_USER"
    export RAVENEST_PG_PASSWORD="$POSTGRES_COMMON_PASSWORD"
}

postgres_common_require_docker() {
    command -v docker >/dev/null 2>&1 || postgres_common_die "docker not found on PATH"
    docker info >/dev/null 2>&1 || postgres_common_die "cannot reach the Docker daemon (check docker group/sudo)"
    docker compose version >/dev/null 2>&1 || postgres_common_die "docker compose is not available"
}

postgres_common_compose() {
    docker compose \
        --project-name "$POSTGRES_PROJECT" \
        --file "$POSTGRES_COMMON_COMPOSE_FILE" \
        "$@"
}

postgres_common_lock_runtime() {
    mkdir -p -- "$POSTGRES_RUNTIME_ROOT"
    if command -v flock >/dev/null 2>&1; then
        exec 9>"$POSTGRES_RUNTIME_ROOT/.postgres-startup.lock"
        flock -w 120 9 || postgres_common_die "timed out waiting for the PostgreSQL startup lock"
    fi
}

postgres_common_prepare_data_dir() {
    POSTGRES_DATA_CREATED=0
    if [ ! -d "$POSTGRES_DATA_DIR" ]; then
        mkdir -p -- "$POSTGRES_DATA_DIR"
        POSTGRES_DATA_CREATED=1
    fi
}

postgres_common_wait_healthy() {
    local container_id status
    container_id="$(postgres_common_compose ps -q postgres 2>/dev/null || true)"
    if [ -z "$container_id" ]; then
        postgres_common_error "PostgreSQL container did not start"
        return 1
    fi

    for _ in $(seq 1 120); do
        status="$(docker inspect \
            --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' \
            "$container_id" 2>/dev/null || true)"
        case "$status" in
            healthy|running)
                return 0
                ;;
            unhealthy|exited|dead)
                postgres_common_error "PostgreSQL container is $status"
                postgres_common_compose logs --tail=40 postgres >&2 || true
                return 1
                ;;
        esac
        sleep 1
    done

    postgres_common_error "timed out waiting for PostgreSQL to become healthy"
    postgres_common_compose logs --tail=40 postgres >&2 || true
    return 1
}

postgres_common_run_init_sql() {
    postgres_common_compose exec -T postgres \
        psql -v ON_ERROR_STOP=1 \
            -U "$POSTGRES_COMMON_USER" \
            -d postgres \
        < "$POSTGRES_COMMON_INIT_SQL"
}

postgres_common_write_env() {
    local host="127.0.0.1"
    local user_encoded password_encoded tmp_file
    user_encoded="$(postgres_common_urlencode "$POSTGRES_COMMON_USER")"
    password_encoded="$(postgres_common_urlencode "$POSTGRES_COMMON_PASSWORD")"
    tmp_file="${POSTGRES_ENV_FILE}.tmp.$$"

    mkdir -p -- "$(dirname -- "$POSTGRES_ENV_FILE")"
    umask 077
    {
        if [ "$POSTGRES_ENV_MODE" = "dev" ]; then
            printf '# Generated by tools/dev_postgres.sh for run_server.sh.\n'
            printf '# Dev-only URLs. Do not source this file for smoke tests.\n'
            printf 'export RAVENEST_GAME_DB_URL=postgresql://%s:%s@%s:%s/ravenest_game\n' \
                "$user_encoded" "$password_encoded" "$host" "$POSTGRES_HOST_PORT"
            printf 'export RAVENEST_CHAT_DB_URL=postgresql://%s:%s@%s:%s/ravenest_chat\n' \
                "$user_encoded" "$password_encoded" "$host" "$POSTGRES_HOST_PORT"
            printf 'export RAVENEST_STAFF_DB_URL=postgresql://%s:%s@%s:%s/ravenest_staff\n' \
                "$user_encoded" "$password_encoded" "$host" "$POSTGRES_HOST_PORT"
            printf 'export RAVENEST_INVITES_DB_URL=postgresql://%s:%s@%s:%s/ravenest_invites\n' \
                "$user_encoded" "$password_encoded" "$host" "$POSTGRES_HOST_PORT"
            printf 'export RAVENEST_DEV_DB_PORT=%s\n' "$POSTGRES_HOST_PORT"
            printf 'export RAVENEST_COOKIE_SECURE=0\n'
        else
            printf '# Generated by tools/test_postgres.sh for smoke tests.\n'
            printf 'export RAVENEST_DB_HOST=%s\n' "$host"
            printf 'export RAVENEST_DB_PORT=%s\n' "$POSTGRES_HOST_PORT"
            printf 'export RAVENEST_DB_USER=%s\n' "$POSTGRES_COMMON_USER"
            printf 'export RAVENEST_DB_PASSWORD=%s\n' "$POSTGRES_COMMON_PASSWORD"
            printf 'export RAVENEST_TEST_DB_PORT=%s\n' "$POSTGRES_HOST_PORT"
        fi
    } > "$tmp_file"
    chmod 600 "$tmp_file"
    mv -f -- "$tmp_file" "$POSTGRES_ENV_FILE"
}

postgres_common_up() {
    postgres_common_require_docker
    postgres_common_resolve_paths
    postgres_common_lock_runtime
    postgres_common_prepare_data_dir

    if [ "$POSTGRES_DATA_CREATED" = "1" ]; then
        printf '%s: created data directory %s\n' "$POSTGRES_PROFILE" "$POSTGRES_DATA_DIR"
    fi

    local compose_up_args=(up -d)
    if [ "$POSTGRES_DATA_CREATED" = "1" ]; then
        compose_up_args+=(--force-recreate)
    fi
    postgres_common_compose "${compose_up_args[@]}" postgres
    postgres_common_wait_healthy || return 1
    postgres_common_run_init_sql || return 1
    postgres_common_write_env

    printf '%s PostgreSQL ready on 127.0.0.1:%s (data: %s)\n' \
        "$POSTGRES_PROFILE" "$POSTGRES_HOST_PORT" "$POSTGRES_DATA_DIR"
}

postgres_common_down() {
    postgres_common_require_docker
    postgres_common_resolve_paths
    postgres_common_compose down --remove-orphans
    printf '%s PostgreSQL stopped (data preserved at %s)\n' \
        "$POSTGRES_PROFILE" "$POSTGRES_DATA_DIR"
}

postgres_common_validate_reset_path() {
    case "$POSTGRES_DATA_DIR" in
        *"/../"*|*"/..")
            postgres_common_die "refusing to remove data directory containing '..': $POSTGRES_DATA_DIR"
            ;;
    esac
    case "$POSTGRES_DATA_DIR" in
        "$POSTGRES_RUNTIME_ROOT"/*) ;;
        *)
            postgres_common_die "refusing to remove data directory outside $POSTGRES_RUNTIME_ROOT: $POSTGRES_DATA_DIR"
            ;;
    esac
    if [ -z "$POSTGRES_DATA_DIR" ] || [ "$POSTGRES_DATA_DIR" = "/" ]; then
        postgres_common_die "refusing to remove invalid data directory"
    fi
}

postgres_common_remove_data_dir() {
    if rm -rf -- "$POSTGRES_DATA_DIR" 2>/dev/null; then
        return 0
    fi

    local parent base
    parent="$(dirname -- "$POSTGRES_DATA_DIR")"
    base="$(basename -- "$POSTGRES_DATA_DIR")"
    mkdir -p -- "$parent"
    docker run --rm --entrypoint /bin/rm \
        -v "$parent:/parent" \
        postgres:16-alpine \
        -rf "/parent/$base"
}

postgres_common_reset() {
    local assume_yes=0
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --yes|-y)
                assume_yes=1
                shift
                ;;
            *)
                postgres_common_die "unknown reset option: $1"
                ;;
        esac
    done

    postgres_common_resolve_paths
    postgres_common_validate_reset_path

    if [ "$assume_yes" != "1" ]; then
        printf 'Delete %s and its PostgreSQL data? [y/N] ' "$POSTGRES_DATA_DIR"
        local answer
        read -r answer || answer=""
        case "$answer" in
            y|Y|yes|YES) ;;
            *)
                printf 'aborted\n'
                return 1
                ;;
        esac
    fi

    postgres_common_require_docker
    postgres_common_compose down --remove-orphans || true
    postgres_common_remove_data_dir
    rm -f -- "$POSTGRES_ENV_FILE"
    printf '%s PostgreSQL data removed\n' "$POSTGRES_PROFILE"
}

postgres_common_status() {
    postgres_common_require_docker
    postgres_common_resolve_paths

    printf 'Profile: %s\nProject: %s\nPort:    %s\nData:    %s\nEnv:     %s\n' \
        "$POSTGRES_PROFILE" "$POSTGRES_PROJECT" "$POSTGRES_HOST_PORT" \
        "$POSTGRES_DATA_DIR" "$POSTGRES_ENV_FILE"

    if [ ! -d "$POSTGRES_DATA_DIR" ]; then
        printf 'State:   not initialized\n'
        return 0
    fi

    postgres_common_compose ps postgres || true

    local container_id status
    container_id="$(postgres_common_compose ps -q postgres 2>/dev/null || true)"
    if [ -z "$container_id" ]; then
        printf 'State:   stopped\n'
        return 0
    fi

    status="$(docker inspect \
        --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' \
        "$container_id" 2>/dev/null || true)"
    printf 'State:   %s\n' "$status"

    if [ "$status" = "healthy" ] || [ "$status" = "running" ]; then
        printf 'Databases:\n'
        postgres_common_compose exec -T postgres \
            psql -U "$POSTGRES_COMMON_USER" -d postgres -Atc \
            "SELECT datname FROM pg_database WHERE datname IN ('ravenest_game','ravenest_chat','ravenest_staff','ravenest_invites') ORDER BY datname;" \
            || true
    fi
}

postgres_common_psql() {
    local database="${1:-ravenest_game}"
    if [ "$#" -gt 0 ]; then
        shift
    fi
    postgres_common_resolve_paths
    postgres_common_require_docker
    if [ -t 0 ]; then
        postgres_common_compose exec postgres \
            psql -U "$POSTGRES_COMMON_USER" -d "$database" "$@"
    else
        postgres_common_compose exec -T postgres \
            psql -U "$POSTGRES_COMMON_USER" -d "$database" "$@"
    fi
}

postgres_common_main() {
    local command="${1:-up}"
    if [ "$#" -gt 0 ]; then
        shift
    fi

    case "$command" in
        up)
            postgres_common_up
            ;;
        down)
            postgres_common_down
            ;;
        reset)
            postgres_common_reset "$@"
            ;;
        status)
            postgres_common_status
            ;;
        psql)
            postgres_common_psql "$@"
            ;;
        logs)
            postgres_common_resolve_paths
            postgres_common_require_docker
            postgres_common_compose logs --tail=100 postgres
            ;;
        help|-h|--help)
            postgres_common_usage
            ;;
        *)
            postgres_common_usage >&2
            exit 1
            ;;
    esac
}
