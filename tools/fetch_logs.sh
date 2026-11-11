#!/usr/bin/env bash
#
# fetch_logs.sh — fetch, filter, and redact Ravenest deployment logs.
#
# install.sh copies this script to ${SERVER_PATH}/tools/fetch_logs.sh on the
# Docker host. It is read-only and never restarts or modifies the stack.
#
# Examples:
#   ./tools/fetch_logs.sh
#   ./tools/fetch_logs.sh --since 30m --grep 'authRefresh|refresh failed'
#   ./tools/fetch_logs.sh --service postgres --since 30m
#   ./tools/fetch_logs.sh --container ravenest-postgres-1 --since 10m
#   ./tools/fetch_logs.sh --output /tmp/ravenest.log
#
# It never prints database URLs or refresh/access tokens by default.

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/_deploy_common.sh
. "$SCRIPT_DIR/_deploy_common.sh"

SERVER_PATH=""
COMPOSE_ENV=""
SERVICE_LIST=()
ALL_SERVICES=0
CONTAINER=""
SINCE="15m"
TAIL="200"
GREP=""
IGNORE_CASE=0
FOLLOW=0
TIMESTAMPS=0
OUTPUT=""
FORCE=0
REDACT=1
NO_HEADER=0

usage() {
    cat <<'EOF'
Usage: fetch_logs.sh [options]

Fetch logs from the Ravenest compose stack or a specific Docker container.

Options:
  --server-path PATH   Deployment directory (default: script parent directory)
  --compose-env PATH   compose.env path (default: SERVER_PATH/compose.env)
  --service NAME       Compose service (repeatable; default: ravenest)
  --all-services       Fetch logs for every compose service
  --container NAME     Read a specific container with `docker logs`
  --since DURATION     Docker time window (default: 15m)
  --tail N             Number of lines, or "all" (default: 200)
  --grep REGEX         Filter lines with grep -E
  --ignore-case        Case-insensitive --grep
  --follow             Follow logs until interrupted
  --timestamps         Include Docker timestamps
  --output FILE        Write output with mode 600
  --force              Overwrite an existing --output file
  --redact             Redact secrets (default)
  --no-redact          Disable redaction
  --raw, --no-header   Print only log lines, no header
  -h, --help           Show this help

--follow cannot be combined with --output.

Exit codes:
  0  logs fetched
  1  Docker/compose failure
  2  usage/config error
EOF
}

die_usage() {
    printf 'error: %s\n' "$*" >&2
    exit 2
}

die_runtime() {
    printf 'error: %s\n' "$*" >&2
    exit 1
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
            --service)
                [ "$#" -ge 2 ] || die_usage "--service requires a value"
                SERVICE_LIST+=("$2")
                shift 2
                ;;
            --all-services)
                ALL_SERVICES=1
                shift
                ;;
            --container)
                [ "$#" -ge 2 ] || die_usage "--container requires a value"
                CONTAINER="$2"
                shift 2
                ;;
            --since)
                [ "$#" -ge 2 ] || die_usage "--since requires a value"
                SINCE="$2"
                shift 2
                ;;
            --tail)
                [ "$#" -ge 2 ] || die_usage "--tail requires a value"
                TAIL="$2"
                shift 2
                ;;
            --grep)
                [ "$#" -ge 2 ] || die_usage "--grep requires a value"
                GREP="$2"
                shift 2
                ;;
            --ignore-case)
                IGNORE_CASE=1
                shift
                ;;
            --follow)
                FOLLOW=1
                shift
                ;;
            --timestamps)
                TIMESTAMPS=1
                shift
                ;;
            --output)
                [ "$#" -ge 2 ] || die_usage "--output requires a value"
                OUTPUT="$2"
                shift 2
                ;;
            --force)
                FORCE=1
                shift
                ;;
            --redact)
                REDACT=1
                shift
                ;;
            --no-redact)
                REDACT=0
                shift
                ;;
            --raw | --no-header)
                NO_HEADER=1
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

    [ -n "$SINCE" ] || die_usage "--since must not be empty"
    case "$SINCE" in
        -*) die_usage "--since must not start with '-'" ;;
    esac
    case "$TAIL" in
        all) ;;
        '' | *[!0-9]*) die_usage "--tail must be a positive integer or 'all'" ;;
    esac
    [ "$TAIL" = "all" ] || [ "$TAIL" -gt 0 ] || die_usage "--tail must be greater than zero"

    if [ -n "$CONTAINER" ]; then
        [ "$ALL_SERVICES" = 0 ] || die_usage "--container cannot be combined with --all-services"
        [ "${#SERVICE_LIST[@]}" -eq 0 ] ||
            die_usage "--container cannot be combined with --service"
    else
        [ "$ALL_SERVICES" = 0 ] || [ "${#SERVICE_LIST[@]}" -eq 0 ] ||
            die_usage "--all-services cannot be combined with --service"
        if [ "$ALL_SERVICES" = 0 ] && [ "${#SERVICE_LIST[@]}" -eq 0 ]; then
            SERVICE_LIST=(ravenest)
        fi
    fi

    if [ -n "$OUTPUT" ] && [ "$FOLLOW" = 1 ]; then
        die_usage "--follow cannot be combined with --output"
    fi
    if [ -n "$OUTPUT" ] && [ -e "$OUTPUT" ] && [ "$FORCE" = 0 ]; then
        die_usage "output file already exists: $OUTPUT (use --force)"
    fi
}

fetch_raw() {
    if [ -n "$CONTAINER" ]; then
        local args=(docker logs --since "$SINCE" --tail "$TAIL")
        [ "$FOLLOW" = 1 ] && args+=(--follow)
        [ "$TIMESTAMPS" = 1 ] && args+=(--timestamps)
        args+=("$CONTAINER")
        "${args[@]}"
        return
    fi

    local args=("${DEPLOY_COMPOSE[@]}" logs --no-color --since "$SINCE" --tail "$TAIL")
    [ "$FOLLOW" = 1 ] && args+=(--follow)
    [ "$TIMESTAMPS" = 1 ] && args+=(--timestamps)
    if [ "${#SERVICE_LIST[@]}" -gt 0 ]; then
        args+=("${SERVICE_LIST[@]}")
    fi
    "${args[@]}"
}

filter_stream() {
    if [ -z "$GREP" ]; then
        cat
        return
    fi
    if [ "$IGNORE_CASE" = 1 ]; then
        grep -E -i -- "$GREP" || true
    else
        grep -E -- "$GREP" || true
    fi
}

redact_stream() {
    if [ "$REDACT" = 0 ]; then
        cat
        return
    fi
    sed -E \
        -e 's#(postgres(ql)?://)[^@[:space:]"]+@#\1***@#g' \
        -e 's#(RAVENEST_[A-Z0-9_]*DB_URL=)[^[:space:]"]+#\1***#g' \
        -e 's#rvs_rt_[A-Za-z0-9_-]+#rvs_rt_***#g' \
        -e 's#rvs_at_[A-Za-z0-9._~+/-]+=*#rvs_at_***#g' \
        -e 's#(Cookie:[[:space:]]*ravenest_refresh=)[^;[:space:]]+#\1***#g' \
        -e 's#(Authorization:[[:space:]]*Bearer[[:space:]]+)[^[:space:]]+#\1***#g' \
        -e 's#(([Pp]assword|[Pp]asswd|[Pp]wd)=)[^[:space:]]+#\1***#g'
}

print_header() {
    [ "$NO_HEADER" = 0 ] || return 0
    printf 'Ravenest logs\n' >&2
    if [ -n "$CONTAINER" ]; then
        printf 'container: %s\n' "$CONTAINER" >&2
    else
        printf 'server path: %s\n' "$SERVER_PATH" >&2
        if [ "$ALL_SERVICES" = 1 ]; then
            printf 'service: all\n' >&2
        else
            printf 'service: %s\n' "${SERVICE_LIST[*]}" >&2
        fi
    fi
    printf 'since: %s\n' "$SINCE" >&2
    printf 'tail: %s\n' "$TAIL" >&2
    if [ -n "$GREP" ]; then
        printf 'grep: %s\n' "$GREP" >&2
    fi
    if [ "$REDACT" = 1 ]; then
        printf 'redaction: on\n' >&2
    else
        printf 'redaction: off\n' >&2
    fi
}

main() {
    parse_args "$@"

    if [ -z "$CONTAINER" ]; then
        deploy_resolve_server_path "$SCRIPT_DIR"
        deploy_load_compose_env
    fi
    deploy_require_docker || exit 1

    print_header

    if [ "$FOLLOW" = 1 ]; then
        if ! fetch_raw | filter_stream | redact_stream; then
            die_runtime "failed to fetch logs"
        fi
        return
    fi

    local temp_file
    temp_file="$(mktemp "${TMPDIR:-/tmp}/ravenest-logs.XXXXXX")" ||
        die_runtime "could not create a temporary file"
    # shellcheck disable=SC2064
    trap "rm -f '$temp_file'" EXIT

    if ! fetch_raw | filter_stream | redact_stream > "$temp_file"; then
        die_runtime "failed to fetch logs"
    fi

    if [ -n "$OUTPUT" ]; then
        [ "$FORCE" = 0 ] || rm -f -- "$OUTPUT"
        umask 077
        cat "$temp_file" > "$OUTPUT" || die_runtime "could not write $OUTPUT"
        chmod 600 "$OUTPUT" || die_runtime "could not set mode 600 on $OUTPUT"
        printf 'saved logs: %s\n' "$OUTPUT" >&2
    else
        cat "$temp_file"
    fi
}

main "$@"
