#!/usr/bin/env bash
#
# deployment_status.sh — fast, read-only status for a Ravenest deployment.
#
# install.sh copies this script to ${SERVER_PATH}/tools/deployment_status.sh.
# Unlike verify_deployment.sh, it does not inspect mounts or inode identity; it
# focuses on whether the app, database, HTTP API, and recent logs are healthy.
#
# It never prints database URLs or passwords.

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/_deploy_common.sh
. "$SCRIPT_DIR/_deploy_common.sh"

SERVER_PATH=""
COMPOSE_ENV=""
APP_CONTAINER="ravenest"
DB_SERVICE="postgres"
BASE_URL="http://127.0.0.1:2290"
TIMEOUT=10
DB_MODE="auto"
DEPLOY_DB_MODE=""
VERBOSE=0
NO_LOGS=0

PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0
SKIP_COUNT=0
APP_MISSING=0

usage() {
    cat <<'EOF'
Usage: deployment_status.sh [options]

Read-only status for the running Ravenest Docker deployment.

Options:
  --server-path PATH   Deployment directory (default: script parent directory)
  --compose-env PATH   compose.env path (default: SERVER_PATH/compose.env)
  --app-container NAME App container name (default: ravenest)
  --db-service NAME    Compose PostgreSQL service (default: postgres)
  --base-url URL       API origin for the health check
                       (default: http://127.0.0.1:2290)
  --db-mode MODE       auto, bundled, or external (default: auto)
  --timeout SECONDS    HTTP timeout (default: 10)
  --no-logs            Skip the recent-error log check
  --verbose            Print additional safe details
  -h, --help           Show this help

Exit codes:
  0  healthy
  1  degraded or failed
  2  usage/config error
EOF
}

die_usage() {
    printf 'error: %s\n' "$*" >&2
    exit 2
}

pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    printf '[PASS] %s\n' "$*"
}

fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    printf '[FAIL] %s\n' "$*"
}

warn() {
    WARN_COUNT=$((WARN_COUNT + 1))
    printf '[WARN] %s\n' "$*"
}

skip() {
    SKIP_COUNT=$((SKIP_COUNT + 1))
    printf '[SKIP] %s\n' "$*"
}

info() {
    if [ "$VERBOSE" = 1 ]; then
        printf '[INFO] %s\n' "$*"
    fi
}

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --server-path)
                [ "$#" -ge 2 ] || die_usage "--server-path requires a value"
                SERVER_PATH="$2"
                shift 2
                ;;
            --compose-env)
                [ "$#" -ge 2 ] || die_usage "--compose-env requires a value"
                COMPOSE_ENV="$2"
                shift 2
                ;;
            --app-container)
                [ "$#" -ge 2 ] || die_usage "--app-container requires a value"
                APP_CONTAINER="$2"
                shift 2
                ;;
            --db-service)
                [ "$#" -ge 2 ] || die_usage "--db-service requires a value"
                DB_SERVICE="$2"
                shift 2
                ;;
            --base-url)
                [ "$#" -ge 2 ] || die_usage "--base-url requires a value"
                BASE_URL="$2"
                shift 2
                ;;
            --db-mode)
                [ "$#" -ge 2 ] || die_usage "--db-mode requires a value"
                DB_MODE="$2"
                shift 2
                ;;
            --timeout)
                [ "$#" -ge 2 ] || die_usage "--timeout requires a value"
                TIMEOUT="$2"
                shift 2
                ;;
            --no-logs)
                NO_LOGS=1
                shift
                ;;
            --verbose)
                VERBOSE=1
                shift
                ;;
            -h | --help)
                usage
                exit 0
                ;;
            *)
                die_usage "unknown argument: $1 (try --help)"
                ;;
        esac
    done

    case "$DB_MODE" in
        auto) DEPLOY_DB_MODE="" ;;
        bundled | external) DEPLOY_DB_MODE="$DB_MODE" ;;
        *) die_usage "--db-mode must be auto, bundled, or external" ;;
    esac
    case "$TIMEOUT" in
        '' | *[!0-9]*) die_usage "--timeout must be a positive integer" ;;
    esac
    [ "$TIMEOUT" -gt 0 ] || die_usage "--timeout must be greater than zero"
    BASE_URL="${BASE_URL%/}"
    case "$BASE_URL" in
        http://* | https://*) ;;
        *) die_usage "--base-url must start with http:// or https://" ;;
    esac
}

check_compose_env_mode() {
    local mode
    mode="$(stat -c '%a' "$COMPOSE_ENV" 2>/dev/null || true)"
    case "$mode" in
        600 | 400) pass "compose.env mode is $mode" ;;
        '') warn "could not read compose.env mode" ;;
        *) warn "compose.env mode is $mode; expected 600" ;;
    esac
}

check_app() {
    local app_id running health image restarts started

    app_id="$(docker inspect --format '{{.Id}}' "$APP_CONTAINER" 2>/dev/null || true)"
    if [ -z "$app_id" ]; then
        APP_MISSING=1
        fail "app container '$APP_CONTAINER' was not found"
        return
    fi
    pass "app container '$APP_CONTAINER' exists"

    running="$(docker inspect --format '{{.State.Running}}' "$APP_CONTAINER" 2>/dev/null || true)"
    if [ "$running" = "true" ]; then
        pass "app container is running"
    else
        fail "app container is not running"
    fi

    health="$(deploy_container_health "$APP_CONTAINER")"
    case "$health" in
        healthy) pass "app health is healthy" ;;
        running) warn "app is running without a healthcheck" ;;
        '' | unknown) warn "app health is unavailable" ;;
        *) fail "app health is '$health'" ;;
    esac

    image="$(deploy_container_image "$APP_CONTAINER")"
    if [ -z "${RAVENEST_IMAGE:-}" ]; then
        warn "RAVENEST_IMAGE is missing from $COMPOSE_ENV"
    elif [ "$image" = "$RAVENEST_IMAGE" ]; then
        pass "app image matches $RAVENEST_IMAGE"
    else
        fail "app image is '$image'; expected '$RAVENEST_IMAGE'"
    fi

    restarts="$(deploy_container_restart_count "$APP_CONTAINER")"
    started="$(deploy_container_started_at "$APP_CONTAINER")"
    info "app started at: ${started:-unknown}"
    info "app restart count: ${restarts:-unknown}"
    case "$restarts" in
        '' | *[!0-9]*) ;;
        *)
            if [ "$restarts" -gt 0 ]; then
                warn "app has restarted $restarts time(s)"
            fi
            ;;
    esac
}

check_http() {
    local status
    status="$(deploy_http_status "$BASE_URL/api/getRegistrationConfig" "$TIMEOUT")"
    if [ "$status" = "200" ]; then
        pass "HTTP API responds at $BASE_URL"
    else
        fail "HTTP API returned ${status:-000} at $BASE_URL"
    fi
}

check_postgres() {
    local pg_id health db_user db_list database missing

    if [ "$DEPLOY_DB_MODE" = "external" ]; then
        skip "external PostgreSQL; bundled database checks skipped"
        return
    fi

    pg_id="$("${DEPLOY_COMPOSE[@]}" ps -q "$DB_SERVICE" 2>/dev/null | head -n 1 || true)"
    if [ -z "$pg_id" ]; then
        fail "bundled PostgreSQL service '$DB_SERVICE' is not running"
        return
    fi
    pass "bundled PostgreSQL container exists"

    if deploy_container_running "$pg_id"; then
        pass "bundled PostgreSQL is running"
    else
        fail "bundled PostgreSQL is not running"
    fi

    health="$(deploy_container_health "$pg_id")"
    case "$health" in
        healthy) pass "bundled PostgreSQL health is healthy" ;;
        running) warn "bundled PostgreSQL has no healthcheck" ;;
        '' | unknown) warn "bundled PostgreSQL health is unavailable" ;;
        *) fail "bundled PostgreSQL health is '$health'" ;;
    esac

    db_user="${RAVENEST_DB_USER:-}"
    if [ -z "$db_user" ]; then
        fail "RAVENEST_DB_USER is missing from $COMPOSE_ENV"
        return
    fi

    if ! db_list="$(docker exec -u postgres "$pg_id" psql \
        -U "$db_user" -d postgres -Atc \
        "SELECT datname FROM pg_database WHERE datname IN ('ravenest_game','ravenest_chat','ravenest_staff','ravenest_invites') ORDER BY 1" \
        2>/dev/null)"; then
        fail "could not query PostgreSQL as role '$db_user'"
        return
    fi

    missing=""
    for database in ravenest_game ravenest_chat ravenest_staff ravenest_invites; do
        if ! printf '%s\n' "$db_list" | grep -qx "$database"; then
            missing="$missing $database"
        fi
    done
    if [ -n "$missing" ]; then
        fail "missing databases:$missing"
    else
        pass "all four ravenest databases exist"
    fi
}

check_disk() {
    local available_kb
    available_kb="$(df -Pk "$SERVER_PATH" 2>/dev/null | awk 'NR == 2 { print $4 }')"
    case "$available_kb" in
        '' | *[!0-9]*)
            warn "could not read free disk space for $SERVER_PATH"
            ;;
        *)
            if [ "$available_kb" -lt 1048576 ]; then
                warn "less than 1 GiB free on $SERVER_PATH"
            else
                pass "disk space on $SERVER_PATH is adequate"
            fi
            ;;
    esac
}

check_logs() {
    [ "$NO_LOGS" = 0 ] || {
        skip "recent log check disabled"
        return
    }
    [ "$APP_MISSING" = 0 ] || {
        skip "recent log check skipped because the app container is missing"
        return
    }
    if [ ! -x "$SCRIPT_DIR/fetch_logs.sh" ]; then
        warn "log fetcher not available; skipping recent log check"
        return
    fi

    local log_output log_count
    log_output="$("$SCRIPT_DIR/fetch_logs.sh" \
        --raw --container "$APP_CONTAINER" \
        --since 15m --tail 20 \
        --grep 'error|exception|segfault|terminate|oom' \
        --ignore-case \
        2>/dev/null || true)"
    if [ -n "$log_output" ]; then
        log_count="$(printf '%s\n' "$log_output" | grep -c . || true)"
    else
        log_count=0
    fi
    if [ "$log_count" -gt 0 ]; then
        warn "$log_count error-like log line(s) in the last 15m"
        if [ "$VERBOSE" = 1 ]; then
            printf '%s\n' "$log_output" | head -n 5
        fi
    else
        pass "no error-like log lines in the last 15m"
    fi
}

print_summary() {
    printf '\n'
    if [ "$FAIL_COUNT" -gt 0 ]; then
        printf 'status: FAILED (%s failed, %s warnings)\n' "$FAIL_COUNT" "$WARN_COUNT"
        exit 1
    fi
    if [ "$WARN_COUNT" -gt 0 ]; then
        printf 'status: DEGRADED (%s warnings)\n' "$WARN_COUNT"
        exit 1
    fi
    printf 'status: OK\n'
    exit 0
}

main() {
    parse_args "$@"

    deploy_resolve_server_path "$SCRIPT_DIR"
    deploy_load_compose_env
    deploy_require_docker || {
        printf '[FAIL] Docker daemon is unavailable\n'
        exit 1
    }

    printf 'Ravenest deployment status\n'
    printf 'server path: %s\n' "$SERVER_PATH"
    printf 'compose env: %s\n' "$COMPOSE_ENV"
    printf 'database mode: %s\n\n' "$DEPLOY_DB_MODE"

    check_compose_env_mode
    check_app
    check_http
    check_postgres
    check_disk
    check_logs
    print_summary
}

main "$@"
