#!/usr/bin/env bash
#
# reload_deployment.sh — trigger a config reload in the running Ravenest app.
#
# install.sh copies this script to ${SERVER_PATH}/tools/reload_deployment.sh
# on the Docker host. Run it there after updating mounted config files:
#
#     ssh user@server /srv/ravenest/tools/reload_deployment.sh
#
# The server reloads all configs and both map caches on SIGHUP. The reload is
# all-or-nothing: a malformed file leaves the previous configuration active.
# This script waits for the server's reload log marker and exits non-zero when
# the reload was aborted or never acknowledged.

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
SERVER_PATH=""
COMPOSE_ENV=""
DB_MODE="auto"
TIMEOUT=30
FALLBACK=1

# shellcheck source=_deploy_common.sh
. "$SCRIPT_DIR/_deploy_common.sh"

usage() {
    cat <<'EOF'
Usage: reload_deployment.sh [options]

Trigger a production-safe config reload (SIGHUP) in the running app.

Options:
  --server-path PATH   Deployment directory (default: script parent directory)
  --compose-env PATH   compose.env path (default: SERVER_PATH/compose.env)
  --db-mode MODE       auto, bundled, or external (default: auto)
  --timeout SECONDS    Wait for the reload marker (default: 30)
  --no-fallback        Do not try `docker compose exec ... kill -HUP 1`
  -h, --help           Show this help

The script:
  - sends SIGHUP through the compose service (tini forwards it to the server)
  - waits for `[GameConfigCache] Reload complete:` or `Reload aborted:`
  - exits 0 only when the server reports a completed reload

A config-only change does not need a new image or a container restart.
EOF
}

log() {
    printf '==> %s\n' "$*"
}

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --server-path)
                [ "$#" -ge 2 ] || deploy_die "--server-path requires a value"
                SERVER_PATH="$2"
                shift 2
                ;;
            --compose-env)
                [ "$#" -ge 2 ] || deploy_die "--compose-env requires a value"
                COMPOSE_ENV="$2"
                shift 2
                ;;
            --db-mode)
                [ "$#" -ge 2 ] || deploy_die "--db-mode requires a value"
                DB_MODE="$2"
                shift 2
                ;;
            --timeout)
                [ "$#" -ge 2 ] || deploy_die "--timeout requires a value"
                TIMEOUT="$2"
                shift 2
                ;;
            --no-fallback)
                FALLBACK=0
                shift
                ;;
            -h | --help)
                usage
                exit 0
                ;;
            *)
                deploy_die "unknown argument: $1 (try --help)"
                ;;
        esac
    done

    case "$DB_MODE" in
        auto | bundled | external) ;;
        *) deploy_die "--db-mode must be auto, bundled, or external" ;;
    esac
    case "$TIMEOUT" in
        '' | *[!0-9]*) deploy_die "--timeout must be a positive integer" ;;
    esac
    [ "$TIMEOUT" -gt 0 ] || deploy_die "--timeout must be greater than zero"
}

send_hup() {
    if ! "${DEPLOY_COMPOSE[@]}" kill -s HUP ravenest >/dev/null 2>&1; then
        return 1
    fi
    return 0
}

send_hup_fallback() {
    "${DEPLOY_COMPOSE[@]}" exec -T ravenest sh -c 'kill -HUP 1' >/dev/null 2>&1
}

# Poll recent logs for a reload marker. Returns 0 (complete), 1 (aborted),
# or 2 (no marker yet).
check_reload_logs() {
    local since="$1"
    local logs
    logs="$("${DEPLOY_COMPOSE[@]}" logs --no-color --since "$since" ravenest 2>&1 || true)"

    if printf '%s\n' "$logs" | grep -q '\[GameConfigCache\] Reload aborted:'; then
        printf '%s\n' "$logs" | grep '\[GameConfigCache\] Reload aborted:' >&2 || true
        return 1
    fi
    if printf '%s\n' "$logs" | grep -q '\[GameConfigCache\] Reload complete:'; then
        printf '%s\n' "$logs" | grep '\[GameConfigCache\] Reload complete:' || true
        return 0
    fi
    return 2
}

wait_for_reload() {
    local since="$1"
    local deadline=$((SECONDS + TIMEOUT))
    while [ "$SECONDS" -lt "$deadline" ]; do
        check_reload_logs "$since"
        case $? in
            0) return 0 ;;
            1) return 1 ;;
        esac
        sleep 1
    done
    return 2
}

main() {
    parse_args "$@"
    deploy_resolve_server_path "$SCRIPT_DIR"
    COMPOSE_ENV="${COMPOSE_ENV:-${SERVER_PATH}/compose.env}"
    DEPLOY_DB_MODE="$DB_MODE"

    deploy_require_docker || exit 2
    deploy_load_compose_env

    if ! deploy_container_running ravenest; then
        deploy_die "ravenest container is not running"
    fi

    printf 'Ravenest config reload\n'
    printf 'server path: %s\n' "$SERVER_PATH"

    # Docker's --since accepts RFC3339. Use a small lead so the marker is
    # included even if the signal is handled within the same second.
    local since
    since="$(date -u -d '2 seconds ago' +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u +%Y-%m-%dT%H:%M:%SZ)"

    log "sending SIGHUP"
    if ! send_hup; then
        deploy_die "failed to send SIGHUP through docker compose"
    fi

    wait_for_reload "$since"
    local status=$?
    if [ "$status" -eq 0 ]; then
        log "reload complete"
        return 0
    fi
    if [ "$status" -eq 1 ]; then
        printf 'error: reload was aborted; the previous configuration is still active\n' >&2
        return 1
    fi

    if [ "$FALLBACK" -eq 1 ]; then
        log "no marker yet; retrying SIGHUP directly in the container"
        since="$(date -u -d '2 seconds ago' +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u +%Y-%m-%dT%H:%M:%SZ)"
        if send_hup_fallback; then
            wait_for_reload "$since"
            status=$?
            if [ "$status" -eq 0 ]; then
                log "reload complete"
                return 0
            fi
            if [ "$status" -eq 1 ]; then
                printf 'error: reload was aborted; the previous configuration is still active\n' >&2
                return 1
            fi
        fi
    fi

    printf 'error: no reload marker within %ss (is the server running?)\n' "$TIMEOUT" >&2
    return 2
}

main "$@"
