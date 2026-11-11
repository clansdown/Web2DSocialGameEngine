#!/usr/bin/env bash
#
# diagnose_auth.sh — diagnose a failed /api/auth/refresh for one session.
#
# install.sh copies this script to ${SERVER_PATH}/tools/diagnose_auth.sh.
# It never prints the refresh token or the returned access token.
#
# Cookie input (preferred: file or environment, not argv):
#   RAVENEST_REFRESH_COOKIE='rvs_rt_...' ./tools/diagnose_auth.sh
#   ./tools/diagnose_auth.sh --cookie-file /tmp/refresh.cookie
#   printf '%s\n' "$cookie" | ./tools/diagnose_auth.sh --cookie-stdin

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/_deploy_common.sh
. "$SCRIPT_DIR/_deploy_common.sh"

BASE_URL="http://127.0.0.1:2290"
APP_CONTAINER="ravenest"
TIMEOUT=10
NO_LOGS=0
DRY_RUN=0
COOKIE_FILE=""
COOKIE_STDIN=0
COOKIE_FROM_ENV=0
COOKIE=""
CURL_CONFIG=""

usage() {
    cat <<'EOF'
Usage: diagnose_auth.sh [options]

Check why the refresh-cookie endpoint is failing for one session.

Options:
  --base-url URL       API origin (default: http://127.0.0.1:2290)
  --cookie-file FILE   Read the ravenest_refresh token from FILE
  --cookie-stdin       Read the ravenest_refresh token from stdin
  --app-container NAME App container name for logs (default: ravenest)
  --timeout SECONDS    HTTP timeout (default: 10)
  --no-logs            Do not fetch recent server logs
  --dry-run            Call refresh without a cookie; expect 401 invalid token
  -h, --help           Show this help

The token can also be supplied through RAVENEST_REFRESH_COOKIE. Cookie input is
sent through a temporary mode-600 curl config, never as a command-line argument.

Exit codes:
  0  refresh succeeded
  1  session invalid; sign in again
  2  server/API failure
  3  usage/config/cookie problem
EOF
}

die_usage() {
    printf 'error: %s\n' "$*" >&2
    exit 3
}

die_config() {
    printf 'error: %s\n' "$*" >&2
    exit 3
}

pass() {
    printf '[PASS] %s\n' "$*"
}

warn() {
    printf '[WARN] %s\n' "$*"
}

fail() {
    printf '[FAIL] %s\n' "$*"
}

trim_cookie() {
    printf '%s' "$1" | tr -d '\r\n' | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//'
}

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --base-url)
                [ "$#" -ge 2 ] || die_usage "--base-url requires a value"
                BASE_URL="$2"
                shift 2
                ;;
            --cookie-file)
                [ "$#" -ge 2 ] || die_usage "--cookie-file requires a value"
                COOKIE_FILE="$2"
                shift 2
                ;;
            --cookie-stdin)
                COOKIE_STDIN=1
                shift
                ;;
            --app-container)
                [ "$#" -ge 2 ] || die_usage "--app-container requires a value"
                APP_CONTAINER="$2"
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
            --dry-run)
                DRY_RUN=1
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

    case "$TIMEOUT" in
        '' | *[!0-9]*) die_usage "--timeout must be a positive integer" ;;
    esac
    [ "$TIMEOUT" -gt 0 ] || die_usage "--timeout must be greater than zero"

    BASE_URL="${BASE_URL%/}"
    case "$BASE_URL" in
        http://* | https://*) ;;
        *) die_usage "--base-url must start with http:// or https://" ;;
    esac

    local cookie_sources=0
    if [ -n "${RAVENEST_REFRESH_COOKIE:-}" ]; then
        COOKIE_FROM_ENV=1
        cookie_sources=$((cookie_sources + 1))
    fi
    [ -z "$COOKIE_FILE" ] || cookie_sources=$((cookie_sources + 1))
    [ "$COOKIE_STDIN" = 0 ] || cookie_sources=$((cookie_sources + 1))
    [ "$cookie_sources" -le 1 ] ||
        die_usage "use only one of RAVENEST_REFRESH_COOKIE, --cookie-file, or --cookie-stdin"
}

load_cookie() {
    if [ "$DRY_RUN" = 1 ]; then
        COOKIE=""
        return
    fi

    if [ "$COOKIE_FROM_ENV" = 1 ]; then
        COOKIE="$(trim_cookie "$RAVENEST_REFRESH_COOKIE")"
    elif [ -n "$COOKIE_FILE" ]; then
        [ -f "$COOKIE_FILE" ] || die_usage "cookie file not found: $COOKIE_FILE"
        local mode
        mode="$(stat -c '%a' "$COOKIE_FILE" 2>/dev/null || true)"
        case "$mode" in
            600 | 400) ;;
            '') warn "could not read cookie file mode" ;;
            *) warn "cookie file mode is $mode; expected 600" ;;
        esac
        COOKIE="$(trim_cookie "$(cat "$COOKIE_FILE")")"
    elif [ "$COOKIE_STDIN" = 1 ]; then
        IFS= read -r COOKIE || true
        COOKIE="$(trim_cookie "$COOKIE")"
    fi

    [ -n "$COOKIE" ] ||
        die_usage "refresh cookie required (use RAVENEST_REFRESH_COOKIE, --cookie-file, or --cookie-stdin)"
}

write_curl_config() {
    local config="$1"
    local body_file="$2"
    local header_file="$3"

    umask 077
    {
        printf 'url = "%s/api/auth/refresh"\n' "$BASE_URL"
        printf 'request = "POST"\n'
        printf 'header = "Content-Type: application/json"\n'
        if [ -n "$COOKIE" ]; then
            printf 'header = "Cookie: ravenest_refresh=%s"\n' "$COOKIE"
        fi
        printf 'data = "{}"\n'
        printf 'silent\nshow-error\n'
        printf 'output = "%s"\n' "$body_file"
        printf 'dump-header = "%s"\n' "$header_file"
        printf 'write-out = "%%{http_code}"\n'
    } > "$config"
    chmod 600 "$config"
}

parse_response_body() {
    local body_file="$1"
    local parsed_file="$2"

    if command -v python3 >/dev/null 2>&1; then
        python3 - "$body_file" > "$parsed_file" <<'PY'
import json
import sys

try:
    with open(sys.argv[1], "r", encoding="utf-8", errors="replace") as handle:
        document = json.load(handle)
except Exception:
    print("json_valid=0")
    print("error=")
    print("token_present=0")
    print("user_id=")
    print("username=")
    print("adult=")
    print("language=")
    print("characters=0")
    raise SystemExit(0)

data = document.get("data")
if not isinstance(data, dict):
    data = {}
characters = data.get("characters")
print("json_valid=1")
print("error=" + str(document.get("error", "")).replace("\n", " "))
print("token_present=" + ("1" if data.get("token") else "0"))
print("user_id=" + str(data.get("user_id", "")))
print("username=" + str(data.get("username", "")))
print("adult=" + str(data.get("adult", "")))
print("language=" + str(data.get("language", "")))
print("characters=" + (str(len(characters)) if isinstance(characters, list) else "0"))
PY
    else
        local parsed_error token_present
        parsed_error="$(grep -o '"error"[[:space:]]*:[[:space:]]*"[^"]*"' "$body_file" 2>/dev/null |
            head -n 1 | sed -E 's/.*"error"[[:space:]]*:[[:space:]]*"([^"]*)"/\1/')"
        token_present=0
        if grep -q '"token"[[:space:]]*:[[:space:]]*"' "$body_file" 2>/dev/null; then
            token_present=1
        fi
        {
            printf 'json_valid=unknown\n'
            printf 'error=%s\n' "$parsed_error"
            printf 'token_present=%s\n' "$token_present"
            printf 'user_id=\nusername=\nadult=\nlanguage=\ncharacters=\n'
        } > "$parsed_file"
    fi
}

read_parsed_value() {
    local parsed_file="$1"
    local key="$2"
    local line
    line="$(grep -m 1 "^${key}=" "$parsed_file" 2>/dev/null || true)"
    printf '%s' "${line#*=}"
}

print_set_cookie() {
    local header_file="$1"
    local line state flags

    line="$(grep -i '^set-cookie: ravenest_refresh=' "$header_file" 2>/dev/null | tail -n 1 || true)"
    if [ -z "$line" ]; then
        printf 'set-cookie: none\n'
        return
    fi
    if printf '%s' "$line" | grep -qi 'Max-Age=0'; then
        state="cleared"
    else
        state="refreshed"
    fi
    flags=""
    printf '%s' "$line" | grep -qi 'HttpOnly' && flags="${flags} HttpOnly"
    printf '%s' "$line" | grep -qi 'Secure' && flags="${flags} Secure"
    if printf '%s' "$line" | grep -qi 'SameSite='; then
        flags="${flags} $(printf '%s' "$line" | grep -io 'SameSite=[^;]*' | head -n 1)"
    fi
    if printf '%s' "$line" | grep -qi 'Path='; then
        flags="${flags} $(printf '%s' "$line" | grep -io 'Path=[^;]*' | head -n 1)"
    fi
    printf 'set-cookie: %s%s\n' "$state" "$flags"
}

fetch_recent_logs() {
    [ "$NO_LOGS" = 0 ] || return 0
    if [ ! -x "$SCRIPT_DIR/fetch_logs.sh" ]; then
        warn "log fetcher not available; skipping recent logs"
        return 0
    fi
    printf '\nRecent refresh-related logs:\n' >&2
    if ! "$SCRIPT_DIR/fetch_logs.sh" \
        --container "$APP_CONTAINER" \
        --since 15m --tail 20 \
        --grep 'authRefresh|refresh failed|could not load session|what=' \
        --ignore-case --no-header; then
        warn "could not fetch recent server logs"
    fi
}

main() {
    parse_args "$@"

    command -v curl >/dev/null 2>&1 || die_config "curl not found on PATH"
    load_cookie

    local temp_dir body_file header_file curl_error
    temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/ravenest-auth.XXXXXX")" ||
        die_config "could not create a temporary directory"
    # shellcheck disable=SC2064
    trap "rm -rf '$temp_dir'" EXIT
    body_file="$temp_dir/body.json"
    header_file="$temp_dir/headers.txt"
    curl_error="$temp_dir/curl.err"
    CURL_CONFIG="$temp_dir/curl.conf"

    printf 'Ravenest auth diagnostic\n'
    printf 'base URL: %s\n' "$BASE_URL"
    printf 'cookie: %s\n' "$([ -n "$COOKIE" ] && printf 'provided (redacted)' || printf 'none (dry run)')"

    local registration_status
    registration_status="$(deploy_http_status "$BASE_URL/api/getRegistrationConfig" "$TIMEOUT")"
    if [ "$registration_status" = "200" ]; then
        pass "server is reachable (registration endpoint 200)"
    else
        warn "registration endpoint returned ${registration_status:-000}"
    fi

    write_curl_config "$CURL_CONFIG" "$body_file" "$header_file"

    local http_status curl_rc
    http_status="$(curl --config "$CURL_CONFIG" 2> "$curl_error")"
    curl_rc=$?

    local parsed_file
    parsed_file="$temp_dir/parsed.txt"
    if [ -f "$body_file" ]; then
        parse_response_body "$body_file" "$parsed_file"
    else
        : > "$parsed_file"
    fi

    local json_valid refresh_error token_present user_id username adult language characters
    json_valid="$(read_parsed_value "$parsed_file" json_valid)"
    refresh_error="$(read_parsed_value "$parsed_file" error)"
    token_present="$(read_parsed_value "$parsed_file" token_present)"
    user_id="$(read_parsed_value "$parsed_file" user_id)"
    username="$(read_parsed_value "$parsed_file" username)"
    adult="$(read_parsed_value "$parsed_file" adult)"
    language="$(read_parsed_value "$parsed_file" language)"
    characters="$(read_parsed_value "$parsed_file" characters)"

    printf 'refresh status: %s\n' "${http_status:-000}"
    if [ -s "$curl_error" ]; then
        printf 'curl: %s\n' "$(tr '\n' ' ' < "$curl_error")"
    fi
    if [ "$json_valid" = "1" ]; then
        printf 'refresh error: %s\n' "${refresh_error:-<none>}"
        printf 'token returned: %s\n' "$([ "$token_present" = "1" ] && printf 'yes (redacted)' || printf 'no')"
        if [ -n "$user_id" ]; then
            printf 'user: %s (%s), adult=%s, language=%s, characters=%s\n' \
                "${username:-?}" "$user_id" "${adult:-?}" "${language:-?}" "${characters:-0}"
        fi
    elif [ "$json_valid" = "unknown" ]; then
        printf 'refresh error: %s\n' "${refresh_error:-<none>}"
        printf 'token returned: %s\n' "$([ "$token_present" = "1" ] && printf 'yes (redacted)' || printf 'no')"
        printf 'response body: JSON summary unavailable (install python3 for details)\n'
    else
        printf 'response body: not valid JSON\n'
    fi
    print_set_cookie "$header_file"

    local exit_code=2
    if [ "$curl_rc" -ne 0 ] || [ -z "$http_status" ] || [ "$http_status" = "000" ]; then
        fail "server or transport failure"
    elif [ "$DRY_RUN" = 1 ]; then
        if [ "$http_status" = "401" ] && [ "$refresh_error" = "invalid_refresh_token" ]; then
            pass "refresh endpoint works and rejects a missing cookie as expected"
            exit_code=0
        else
            fail "refresh endpoint did not return the expected 401 invalid_refresh_token"
        fi
    elif [ "$http_status" = "200" ] && [ "$token_present" = "1" ]; then
        pass "refresh succeeded"
        exit_code=0
    elif [ "$http_status" = "401" ] && [ "$refresh_error" = "invalid_refresh_token" ]; then
        fail "session is invalid; sign in again"
        exit_code=1
    elif [ "$http_status" = "401" ]; then
        fail "refresh was rejected with a server-side error; inspect the logs above"
    elif [ "$http_status" = "200" ] && [ -n "$refresh_error" ]; then
        fail "refresh handler returned an error as HTTP 200; inspect the logs above"
    else
        fail "unexpected refresh response"
    fi

    fetch_recent_logs
    exit "$exit_code"
}

main "$@"
