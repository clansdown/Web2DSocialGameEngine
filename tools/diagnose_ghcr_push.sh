#!/usr/bin/env bash
#
# Diagnose GHCR push authorization for the developer image publishing config.
#
# Reads .publish_engine by default. This is read-only by default. It inspects
# the token identity, token scopes, namespace, package, and linked-repository
# access. With --push-test it also pushes a small throwaway image so the
# registry's actual answer is visible.
#
# The token is never printed and is passed to curl through stdin config, not
# through command-line arguments.

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$SCRIPT_DIR"

PUBLISH_FILE=".publish_engine"
OPT_IMAGE=""
OPT_USERNAME=""
OPT_TAG=""
OPT_TOKEN_FILE=""
PUSH_TEST=0
NO_NETWORK=0
VERBOSE=0

EXIT_CODE=0
PUSH_TEST_OK=0
PUSH_TEST_DENIED=0

SERVER_IMAGE="${SERVER_IMAGE:-}"
SERVER_IMAGE_TAG="${SERVER_IMAGE_TAG:-}"
SERVER_REGISTRY="${SERVER_REGISTRY:-}"
SERVER_REGISTRY_USERNAME="${SERVER_REGISTRY_USERNAME:-}"
SERVER_REGISTRY_TOKEN="${SERVER_REGISTRY_TOKEN:-}"

IMAGE_REGISTRY=""
IMAGE_OWNER=""
IMAGE_PACKAGE=""
IMAGE_TAG=""

TOKEN_KIND=""
TOKEN_LOGIN=""
TOKEN_SCOPES=""
TOKEN_HAS_REPO=0
TOKEN_USABLE=0
EFFECTIVE_USERNAME=""

NAMESPACE_TYPE=""
NAMESPACE_LOGIN=""
PACKAGE_EXISTS=0
PACKAGE_VISIBILITY=""
PACKAGE_REPOSITORY=""
PACKAGE_HTML_URL=""
REPO_PRIVATE=""
REPO_PUSH_PERMISSION=""

DOCKER_DAEMON_OK=0
DOCKER_AUTH_ENTRY=""
DOCKER_CREDS_STORE=""
DOCKER_CRED_HELPER=""

API_STATUS=""
API_BODY=""
API_HEADERS=""

CHECKS=()
FINDING_SEVERITY=()
FINDING_CODE=()
FINDING_MESSAGE=()
FINDING_FIX=()
TEMP_FILES=()
TEMP_DIRS=()

usage() {
    cat <<'EOF'
Usage: tools/diagnose_ghcr_push.sh [options]

Diagnose why a GitHub Container Registry push is denied.

Options:
  --publish-config FILE  Read publishing values from FILE (default: .publish_engine)
  --image IMAGE          Override SERVER_IMAGE
  --username USER        Override SERVER_REGISTRY_USERNAME
  --tag TAG              Override SERVER_IMAGE_TAG
  --token-file FILE      Read the token from FILE instead of the publish config
  --push-test            Push a throwaway diagnostic image to verify write access
  --no-network           Skip GitHub API and registry checks
  --verbose              Print HTTP status codes and curl network errors
  -h, --help             Show this help

The token is never printed. When --push-test is used, it creates a package named
ghcr.io/<owner>/ravenest-ghcr-diagnostic; remove it from GitHub Packages when
you are done.
EOF
}

die_usage() {
    printf 'error: %s\n' "$*" >&2
    printf 'Try --help for usage.\n' >&2
    exit 1
}

cleanup() {
    local path
    if [ "${#TEMP_FILES[@]}" -gt 0 ]; then
        for path in "${TEMP_FILES[@]}"; do
            rm -f -- "$path"
        done
    fi
    if [ "${#TEMP_DIRS[@]}" -gt 0 ]; then
        for path in "${TEMP_DIRS[@]}"; do
            rm -rf -- "$path"
        done
    fi
}

add_check() {
    CHECKS+=("$*")
}

add_ok() {
    CHECKS+=("[OK] $*")
}

add_warn() {
    CHECKS+=("[WARN] $*")
}

add_skip() {
    CHECKS+=("[SKIP] $*")
}

add_finding() {
    local severity="$1"
    local code="$2"
    local message="$3"
    local fix="${4:-}"

    FINDING_SEVERITY+=("$severity")
    FINDING_CODE+=("$code")
    FINDING_MESSAGE+=("$message")
    FINDING_FIX+=("$fix")

    if [ "$severity" != "fail" ]; then
        return 0
    fi

    if [ "$EXIT_CODE" -ne 0 ]; then
        return 0
    fi

    case "$code" in
        TOKEN_* | AUTH_* | PUSH_LOGIN_FAILED)
            EXIT_CODE=2
            ;;
        NAMESPACE_* | PACKAGE_* | REPO_* | PUSH_DENIED)
            EXIT_CODE=3
            ;;
        DOCKER_* | PUSH_PULL_FAILED | PUSH_TAG_FAILED)
            EXIT_CODE=4
            ;;
        NETWORK_* | API_*)
            EXIT_CODE=5
            ;;
        *)
            EXIT_CODE=1
            ;;
    esac
}

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --publish-config)
                [ "$#" -ge 2 ] || die_usage "--publish-config requires a file"
                PUBLISH_FILE="$2"
                shift 2
                ;;
            --image)
                [ "$#" -ge 2 ] || die_usage "--image requires a value"
                OPT_IMAGE="$2"
                shift 2
                ;;
            --username)
                [ "$#" -ge 2 ] || die_usage "--username requires a value"
                OPT_USERNAME="$2"
                shift 2
                ;;
            --tag)
                [ "$#" -ge 2 ] || die_usage "--tag requires a value"
                OPT_TAG="$2"
                shift 2
                ;;
            --token-file)
                [ "$#" -ge 2 ] || die_usage "--token-file requires a file"
                OPT_TOKEN_FILE="$2"
                shift 2
                ;;
            --push-test)
                PUSH_TEST=1
                shift
                ;;
            --no-network)
                NO_NETWORK=1
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
                die_usage "unknown argument: $1"
                ;;
        esac
    done

    if [ "$PUSH_TEST" = "1" ] && [ "$NO_NETWORK" = "1" ]; then
        die_usage "--push-test cannot be combined with --no-network"
    fi
}

load_publish_config() {
    if [ ! -f "$PUBLISH_FILE" ]; then
        if [ "$PUBLISH_FILE" != ".publish_engine" ]; then
            die_usage "publish config not found: $PUBLISH_FILE"
        fi
        return 0
    fi

    # .publish_engine is a private shell fragment of simple assignments.
    local config_path="$PUBLISH_FILE"
    case "$config_path" in
        /*) ;;
        *) config_path="./$config_path" ;;
    esac
    # shellcheck disable=SC1090
    set +u
    . "$config_path"
    set -u
}

apply_overrides() {
    if [ -n "$OPT_IMAGE" ]; then
        SERVER_IMAGE="$OPT_IMAGE"
    fi
    if [ -n "$OPT_USERNAME" ]; then
        SERVER_REGISTRY_USERNAME="$OPT_USERNAME"
    fi
    if [ -n "$OPT_TAG" ]; then
        SERVER_IMAGE_TAG="$OPT_TAG"
    fi

    SERVER_IMAGE="${SERVER_IMAGE:-}"
    SERVER_IMAGE_TAG="${SERVER_IMAGE_TAG:-}"
    SERVER_REGISTRY="${SERVER_REGISTRY:-}"
    SERVER_REGISTRY_USERNAME="${SERVER_REGISTRY_USERNAME:-}"
    SERVER_REGISTRY_TOKEN="${SERVER_REGISTRY_TOKEN:-}"

    if [ -n "$OPT_TOKEN_FILE" ]; then
        [ -f "$OPT_TOKEN_FILE" ] ||
            die_usage "token file not found: $OPT_TOKEN_FILE"
        SERVER_REGISTRY_TOKEN="$(tr -d '\r\n' < "$OPT_TOKEN_FILE")"
    fi
}

require_commands() {
    local missing=()
    local command_name

    for command_name in curl python3; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            missing+=("$command_name")
        fi
    done

    if [ "${#missing[@]}" -gt 0 ]; then
        add_finding fail CONFIG_MISSING_TOOL \
            "missing required command(s): ${missing[*]}" \
            "Install curl and python3, then rerun the diagnostic."
        return 1
    fi
    return 0
}

derive_image_parts() {
    case "$SERVER_IMAGE" in
        ghcr.io/*) ;;
        *)
            add_finding fail CONFIG_NOT_GHCR \
                "SERVER_IMAGE is not a ghcr.io image: ${SERVER_IMAGE:-<empty>}" \
                "Use SERVER_IMAGE=ghcr.io/<owner>/<package>."
            return 1
            ;;
    esac

    case "$SERVER_IMAGE" in
        *[!a-z0-9._:/-]*)
            add_finding fail CONFIG_BAD_IMAGE_CASE \
                "SERVER_IMAGE must be lowercase: $SERVER_IMAGE" \
                "Use a lowercase GHCR package path, for example ghcr.io/clansdown/ravenest-server."
            return 1
            ;;
    esac

    local rest="${SERVER_IMAGE#ghcr.io/}"
    IMAGE_OWNER="${rest%%/*}"
    IMAGE_PACKAGE="${rest#*/}"

    if [ -z "$IMAGE_OWNER" ] || [ "$IMAGE_OWNER" = "$rest" ] || [ -z "$IMAGE_PACKAGE" ]; then
        add_finding fail CONFIG_BAD_IMAGE \
            "SERVER_IMAGE must be ghcr.io/<owner>/<package>: $SERVER_IMAGE" \
            "Set SERVER_IMAGE to a lowercase package path, for example ghcr.io/clansdown/ravenest-server."
        return 1
    fi

    case "$IMAGE_PACKAGE" in
        *:*)
            IMAGE_TAG="${IMAGE_PACKAGE##*:}"
            IMAGE_PACKAGE="${IMAGE_PACKAGE%:*}"
            add_warn "SERVER_IMAGE included a tag; use SERVER_IMAGE_TAG instead"
            ;;
    esac

    IMAGE_REGISTRY="ghcr.io"
    if [ -z "$SERVER_REGISTRY" ]; then
        SERVER_REGISTRY="$IMAGE_REGISTRY"
    elif [ "$SERVER_REGISTRY" != "$IMAGE_REGISTRY" ]; then
        add_finding fail CONFIG_REGISTRY_MISMATCH \
            "SERVER_REGISTRY ($SERVER_REGISTRY) does not match SERVER_IMAGE ($IMAGE_REGISTRY)" \
            "Set SERVER_REGISTRY=ghcr.io or remove it and let the script derive it."
        return 1
    fi

    IMAGE_TAG="${SERVER_IMAGE_TAG:-${IMAGE_TAG:-}}"
    return 0
}

urlencode() {
    python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$1"
}

json_get() {
    local file="$1"
    shift
    python3 - "$file" "$@" <<'PY'
import json
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as handle:
        value = json.load(handle)
except Exception:
    sys.exit(1)

for key in sys.argv[2:]:
    if isinstance(value, dict) and key in value:
        value = value[key]
    else:
        sys.exit(1)

if value is None:
    print("")
elif isinstance(value, bool):
    print("true" if value else "false")
elif isinstance(value, (dict, list)):
    print(json.dumps(value, separators=(",", ":")))
else:
    print(value)
PY
}

header_value() {
    local file="$1"
    local wanted="$2"
    awk -v wanted="$wanted" '
        BEGIN { wanted = tolower(wanted) }
        {
            line = $0
            sub(/\r$/, "", line)
            colon = index(line, ":")
            if (colon == 0) next
            key = tolower(substr(line, 1, colon - 1))
            if (key != wanted) next
            value = substr(line, colon + 1)
            sub(/^[ \t]+/, "", value)
            print value
            exit
        }
    ' "$file"
}

api_request() {
    local url="$1"
    local body headers errors status

    body="$(mktemp)" || {
        add_finding fail CONFIG_TEMP_FILE "could not create a temporary file"
        API_STATUS="000"
        return 1
    }
    headers="$(mktemp)" || {
        add_finding fail CONFIG_TEMP_FILE "could not create a temporary file"
        API_STATUS="000"
        return 1
    }
    errors="$(mktemp)" || {
        add_finding fail CONFIG_TEMP_FILE "could not create a temporary file"
        API_STATUS="000"
        return 1
    }
    TEMP_FILES+=("$body" "$headers" "$errors")

    status="$(
        {
            printf 'silent\n'
            printf 'show-error\n'
            printf 'location\n'
            printf 'connect-timeout = 10\n'
            printf 'max-time = 30\n'
            printf 'header = "Accept: application/vnd.github+json"\n'
            printf 'header = "X-GitHub-Api-Version: 2022-11-28"\n'
            printf 'header = "User-Agent: ravenest-ghcr-diagnostic"\n'
            if [ -n "$SERVER_REGISTRY_TOKEN" ]; then
                printf 'header = "Authorization: Bearer %s"\n' "$SERVER_REGISTRY_TOKEN"
            fi
            printf 'dump-header = "%s"\n' "$headers"
            printf 'output = "%s"\n' "$body"
            printf 'write-out = "%%{http_code}"\n'
        } | curl --config - "$url" 2>"$errors"
    )" || status="000"

    API_STATUS="${status:-000}"
    API_BODY="$body"
    API_HEADERS="$headers"

    if [ "$VERBOSE" = "1" ]; then
        printf '[api] GET %s -> %s\n' "$url" "$API_STATUS"
        if [ "$API_STATUS" = "000" ] && [ -s "$errors" ]; then
            printf '[api] %s\n' "$(tail -n 1 "$errors")"
        fi
    fi

    return 0
}

has_scope() {
    local wanted="$1"
    local compact="${TOKEN_SCOPES// /}"
    case ",${compact}," in
        *",${wanted},"*) return 0 ;;
    esac
    return 1
}

detect_token_kind() {
    case "$1" in
        ghp_*) printf 'classic\n' ;;
        github_pat_*) printf 'fine-grained\n' ;;
        gho_*) printf 'oauth\n' ;;
        ghu_*) printf 'user-to-server\n' ;;
        ghs_*) printf 'server-to-server\n' ;;
        ghr_*) printf 'refresh\n' ;;
        *) printf 'unknown\n' ;;
    esac
}

check_docker() {
    if ! command -v docker >/dev/null 2>&1; then
        add_finding fail DOCKER_NOT_FOUND \
            "docker is not installed or not on PATH" \
            "Install Docker and make sure the docker command works."
        return
    fi

    local version
    if command -v timeout >/dev/null 2>&1; then
        version="$(timeout 10 docker info --format '{{.ServerVersion}}' 2>/dev/null)" || version=""
    else
        version="$(docker info --format '{{.ServerVersion}}' 2>/dev/null)" || version=""
    fi

    if [ -z "$version" ]; then
        add_finding fail DOCKER_UNREACHABLE \
            "cannot reach the Docker daemon at the default socket" \
            "Add your user to the docker group, start Docker, or fix DOCKER_HOST."
        return
    fi

    DOCKER_DAEMON_OK=1
    add_ok "docker daemon: ${version}"
    check_docker_config
}

check_docker_config() {
    local config="${DOCKER_CONFIG:-$HOME/.docker}/config.json"
    if [ ! -f "$config" ]; then
        add_warn "no Docker config at $config (docker login may not have run)"
        return
    fi

    local parsed
    parsed="$(python3 - "$config" <<'PY'
import json
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as handle:
        data = json.load(handle)
except Exception:
    print("invalid||")
    sys.exit(0)

auths = data.get("auths", {}) or {}
has_auth = "yes" if ("ghcr.io" in auths or "https://ghcr.io" in auths) else "no"
creds_store = data.get("credsStore", "") or ""
cred_helpers = data.get("credHelpers", {}) or {}
cred_helper = cred_helpers.get("ghcr.io", "") or ""
print(f"{has_auth}|{creds_store}|{cred_helper}")
PY
)" || parsed="invalid||"

    IFS='|' read -r DOCKER_AUTH_ENTRY DOCKER_CREDS_STORE DOCKER_CRED_HELPER <<< "$parsed"

    if [ "$DOCKER_AUTH_ENTRY" = "yes" ]; then
        add_ok "docker config has a ghcr.io credential entry"
    else
        add_warn "docker config has no ghcr.io credential entry"
    fi
    if [ -n "$DOCKER_CREDS_STORE" ]; then
        add_check "[INFO] Docker credsStore: $DOCKER_CREDS_STORE"
    fi
    if [ -n "$DOCKER_CRED_HELPER" ]; then
        add_check "[INFO] Docker credential helper for ghcr.io: $DOCKER_CRED_HELPER"
    fi
}

check_token() {
    if [ -z "$SERVER_REGISTRY_TOKEN" ]; then
        add_warn "no SERVER_REGISTRY_TOKEN configured; Docker may use cached credentials"
        return
    fi

    TOKEN_KIND="$(detect_token_kind "$SERVER_REGISTRY_TOKEN")"

    if [ "$NO_NETWORK" = "1" ]; then
        add_skip "token identity and scopes (--no-network)"
        return
    fi

    api_request "https://api.github.com/user"
    if [ "$API_STATUS" = "000" ]; then
        add_finding fail NETWORK_ERROR \
            "could not reach https://api.github.com/user" \
            "Check network access, DNS, and proxy settings, then rerun."
        return
    fi
    if [ "$API_STATUS" != "200" ]; then
        add_finding fail TOKEN_INVALID \
            "GET /user returned HTTP ${API_STATUS}; the token is invalid, expired, or rejected" \
            "Create a fresh classic PAT with write:packages and update SERVER_REGISTRY_TOKEN."
        return
    fi

    TOKEN_LOGIN="$(json_get "$API_BODY" login || true)"
    TOKEN_SCOPES="$(header_value "$API_HEADERS" "x-oauth-scopes")"

    if [ -z "$TOKEN_LOGIN" ]; then
        add_finding fail TOKEN_INVALID \
            "GET /user succeeded but did not return a login" \
            "Check the token and GitHub API status."
        return
    fi

    TOKEN_USABLE=1
    add_ok "token identity: ${TOKEN_LOGIN} (${TOKEN_KIND})"

    if [ -n "$SERVER_REGISTRY_USERNAME" ] && [ "$SERVER_REGISTRY_USERNAME" != "$TOKEN_LOGIN" ]; then
        add_finding fail TOKEN_USER_MISMATCH \
            "token belongs to '${TOKEN_LOGIN}', but SERVER_REGISTRY_USERNAME is '${SERVER_REGISTRY_USERNAME}'" \
            "Set SERVER_REGISTRY_USERNAME=${TOKEN_LOGIN} (the token owner), or use a token owned by ${SERVER_REGISTRY_USERNAME}."
    elif [ -z "$SERVER_REGISTRY_USERNAME" ]; then
        add_warn "SERVER_REGISTRY_USERNAME is empty; using token owner '${TOKEN_LOGIN}' for checks"
    fi

    EFFECTIVE_USERNAME="${SERVER_REGISTRY_USERNAME:-$TOKEN_LOGIN}"

    case "$TOKEN_KIND" in
        classic)
            if has_scope "write:packages"; then
                add_ok "token scopes include write:packages"
            else
                add_finding fail TOKEN_SCOPE_MISSING \
                    "classic token scopes do not include write:packages (reported: ${TOKEN_SCOPES:-none})" \
                    "Edit the token and enable write:packages, then update SERVER_REGISTRY_TOKEN."
            fi
            if ! has_scope "read:packages"; then
                add_warn "token scopes do not include read:packages"
            fi
            if has_scope "repo"; then
                TOKEN_HAS_REPO=1
            fi
            ;;
        fine-grained)
            add_warn "fine-grained token: verify Packages: Read and write for the ${IMAGE_OWNER} namespace"
            ;;
        *)
            add_warn "token type is ${TOKEN_KIND}; expected a classic PAT or fine-grained PAT"
            ;;
    esac
}

check_namespace() {
    if [ "$NO_NETWORK" = "1" ]; then
        add_skip "namespace check (--no-network)"
        return
    fi

    api_request "https://api.github.com/users/${IMAGE_OWNER}"
    if [ "$API_STATUS" = "200" ]; then
        NAMESPACE_TYPE="$(json_get "$API_BODY" type || true)"
        NAMESPACE_LOGIN="$(json_get "$API_BODY" login || true)"
        if [ "$NAMESPACE_TYPE" != "Organization" ]; then
            NAMESPACE_TYPE="User"
        fi
    elif [ "$API_STATUS" = "404" ]; then
        api_request "https://api.github.com/orgs/${IMAGE_OWNER}"
        if [ "$API_STATUS" = "200" ]; then
            NAMESPACE_TYPE="Organization"
            NAMESPACE_LOGIN="$(json_get "$API_BODY" login || true)"
        elif [ "$API_STATUS" = "404" ]; then
            add_finding fail NAMESPACE_NOT_FOUND \
                "namespace '${IMAGE_OWNER}' was not found via /users or /orgs" \
                "Check SERVER_IMAGE owner. For a personal account it must be your GitHub username."
            return
        elif [ "$API_STATUS" = "000" ]; then
            add_finding fail NETWORK_ERROR \
                "could not reach the GitHub namespace API for '${IMAGE_OWNER}'" \
                "Check network access, DNS, and proxy settings, then rerun."
            return
        else
            add_warn "namespace API returned HTTP ${API_STATUS} for '${IMAGE_OWNER}'"
            return
        fi
    elif [ "$API_STATUS" = "000" ]; then
        add_finding fail NETWORK_ERROR \
            "could not reach the GitHub namespace API for '${IMAGE_OWNER}'" \
            "Check network access, DNS, and proxy settings, then rerun."
        return
    else
        add_warn "namespace API returned HTTP ${API_STATUS} for '${IMAGE_OWNER}'"
        return
    fi

    if [ -z "$NAMESPACE_LOGIN" ]; then
        NAMESPACE_LOGIN="$IMAGE_OWNER"
    fi

    add_ok "namespace: ${NAMESPACE_LOGIN} (${NAMESPACE_TYPE})"

    if [ "$NAMESPACE_TYPE" = "User" ] && [ -n "$TOKEN_LOGIN" ] && [ "$TOKEN_LOGIN" != "$NAMESPACE_LOGIN" ]; then
        add_finding fail NAMESPACE_MISMATCH \
            "personal namespace '${NAMESPACE_LOGIN}' is not the token owner '${TOKEN_LOGIN}'" \
            "For a personal account, SERVER_IMAGE must use the token owner as its namespace."
    fi
}

check_package() {
    if [ "$NO_NETWORK" = "1" ]; then
        add_skip "package check (--no-network)"
        return
    fi

    local encoded_package
    encoded_package="$(urlencode "$IMAGE_PACKAGE")"

    local endpoint
    if [ "$NAMESPACE_TYPE" = "Organization" ]; then
        endpoint="https://api.github.com/orgs/${IMAGE_OWNER}/packages/container/${encoded_package}"
    else
        endpoint="https://api.github.com/users/${IMAGE_OWNER}/packages/container/${encoded_package}"
    fi

    api_request "$endpoint"
    case "$API_STATUS" in
        200)
            PACKAGE_EXISTS=1
            PACKAGE_VISIBILITY="$(json_get "$API_BODY" visibility || true)"
            PACKAGE_REPOSITORY="$(json_get "$API_BODY" repository full_name || true)"
            PACKAGE_HTML_URL="$(json_get "$API_BODY" html_url || true)"
            add_ok "package exists: ${IMAGE_PACKAGE} (${PACKAGE_VISIBILITY:-unknown visibility})"
            if [ -z "$PACKAGE_REPOSITORY" ]; then
                add_warn "package is not linked to a source repository; access is package-managed"
            else
                add_check "[INFO] package source repository: ${PACKAGE_REPOSITORY}"
            fi
            ;;
        404)
            PACKAGE_EXISTS=0
            add_warn "package '${IMAGE_PACKAGE}' was not found or is not visible to this token"
            if [ "$NAMESPACE_TYPE" = "Organization" ]; then
                add_check "[INFO] For an organization namespace, the first push creates the package only if the token user may create packages in ${IMAGE_OWNER}."
            elif [ -n "$TOKEN_LOGIN" ] && [ "$TOKEN_LOGIN" = "$IMAGE_OWNER" ]; then
                add_check "[INFO] Since the token owner owns the namespace, the first successful push should create the package."
            elif [ -n "$TOKEN_LOGIN" ]; then
                add_finding fail PACKAGE_NOT_FOUND \
                    "package is missing and the token owner '${TOKEN_LOGIN}' does not own namespace '${IMAGE_OWNER}'" \
                    "Use the namespace owned by the token, or authenticate as an account with access to ${IMAGE_OWNER}."
            fi
            ;;
        401 | 403)
            add_finding fail PACKAGE_NO_ACCESS \
                "package API returned HTTP ${API_STATUS}" \
                "The token cannot see or manage this package. Verify token scopes and package access."
            ;;
        000)
            add_finding fail NETWORK_ERROR \
                "could not reach the GitHub package API" \
                "Check network access, DNS, and proxy settings, then rerun."
            ;;
        *)
            add_warn "package API returned HTTP ${API_STATUS}"
            ;;
    esac
}

check_linked_repository() {
    if [ -z "$PACKAGE_REPOSITORY" ]; then
        return
    fi
    if [ "$NO_NETWORK" = "1" ]; then
        add_skip "linked repository check (--no-network)"
        return
    fi

    api_request "https://api.github.com/repos/${PACKAGE_REPOSITORY}"
    if [ "$API_STATUS" = "000" ]; then
        add_finding fail NETWORK_ERROR \
            "could not reach the GitHub repository API for '${PACKAGE_REPOSITORY}'" \
            "Check network access, DNS, and proxy settings, then rerun."
        return
    fi
    if [ "$API_STATUS" != "200" ]; then
        add_warn "linked repository '${PACKAGE_REPOSITORY}' returned HTTP ${API_STATUS}"
        return
    fi

    REPO_PRIVATE="$(json_get "$API_BODY" private || true)"
    REPO_PUSH_PERMISSION="$(json_get "$API_BODY" permissions push || true)"

    add_check "[INFO] linked repository: ${PACKAGE_REPOSITORY} (private=${REPO_PRIVATE:-unknown}, push=${REPO_PUSH_PERMISSION:-unknown})"

    if [ "$REPO_PRIVATE" = "true" ] && [ "$TOKEN_KIND" = "classic" ] && [ "$TOKEN_HAS_REPO" != "1" ]; then
        add_warn "package inherits access from a private repository, but the PAT has no repo scope"
    fi

    if [ -z "$REPO_PUSH_PERMISSION" ]; then
        add_warn "could not determine push permission for linked repository ${PACKAGE_REPOSITORY}"
    elif [ "$REPO_PUSH_PERMISSION" != "true" ]; then
        add_finding fail REPO_NO_PUSH \
            "the token user does not have push access to ${PACKAGE_REPOSITORY}" \
            "Grant the token owner write access to the linked repository, or change the package's inherited access."
    fi
}

run_push_test() {
    if [ "$PUSH_TEST" != "1" ]; then
        add_skip "push test (use --push-test to verify write access)"
        return
    fi
    if [ "$NO_NETWORK" = "1" ]; then
        add_skip "push test (--no-network)"
        return
    fi
    if [ "$DOCKER_DAEMON_OK" != "1" ]; then
        add_skip "push test (Docker daemon unavailable)"
        return
    fi
    if [ -z "$SERVER_REGISTRY_TOKEN" ]; then
        add_finding fail PUSH_LOGIN_FAILED \
            "cannot run push test without SERVER_REGISTRY_TOKEN" \
            "Set the token or use an existing Docker login manually."
        return
    fi

    local username="${EFFECTIVE_USERNAME:-${SERVER_REGISTRY_USERNAME:-$TOKEN_LOGIN}}"
    if [ -z "$username" ]; then
        add_finding fail PUSH_LOGIN_FAILED \
            "no username is available for the push test" \
            "Set SERVER_REGISTRY_USERNAME."
        return
    fi

    local temp_config
    temp_config="$(mktemp -d)" || {
        add_finding fail CONFIG_TEMP_FILE "could not create a temporary Docker config directory"
        return
    }
    TEMP_DIRS+=("$temp_config")

    if ! printf '%s' "$SERVER_REGISTRY_TOKEN" |
        DOCKER_CONFIG="$temp_config" docker login ghcr.io -u "$username" --password-stdin >/dev/null 2>&1; then
        add_finding fail PUSH_LOGIN_FAILED \
            "docker login ghcr.io failed for '${username}' using the configured token" \
            "Verify the token and the username."
        return
    fi

    local test_tag
    test_tag="ravenest-ghcr-diagnostic:$(date -u +%Y%m%dT%H%M%SZ)"
    local test_image="ghcr.io/${IMAGE_OWNER}/${test_tag}"

    if ! DOCKER_CONFIG="$temp_config" docker pull hello-world >/dev/null 2>&1; then
        add_finding fail PUSH_PULL_FAILED \
            "could not pull hello-world for the push test" \
            "Check Docker network access and authentication, then rerun."
        return
    fi

    if ! DOCKER_CONFIG="$temp_config" docker tag hello-world "$test_image" >/dev/null 2>&1; then
        add_finding fail PUSH_TAG_FAILED \
            "could not tag the diagnostic image as ${test_image}" \
            "Check the Docker daemon."
        return
    fi

    local output status
    output="$(DOCKER_CONFIG="$temp_config" docker push "$test_image" 2>&1)"
    status=$?

    if [ "$status" -eq 0 ]; then
        PUSH_TEST_OK=1
        add_ok "push test succeeded: ${test_image}"
        add_check "[INFO] Remove the diagnostic package from GitHub Packages when finished."
    else
        PUSH_TEST_DENIED=1
        add_finding fail PUSH_DENIED \
            "diagnostic push failed: $(printf '%s' "$output" | tail -n 3 | tr '\n' ' ')" \
            "The registry denied the write. Check token scopes, the login account, and package access."
    fi
}

print_report() {
    printf '\nGHCR push diagnostic\n'
    printf '  image:      %s\n' "${SERVER_IMAGE:-<not configured>}"
    printf '  registry:   %s\n' "${SERVER_REGISTRY:-<derived>}"
    printf '  owner:      %s\n' "${IMAGE_OWNER:-<unknown>}"
    printf '  package:    %s\n' "${IMAGE_PACKAGE:-<unknown>}"
    printf '  tag:        %s\n' "${IMAGE_TAG:-<derived by publish_engine_server_image.sh>}"
    printf '  username:   %s\n' "${EFFECTIVE_USERNAME:-${SERVER_REGISTRY_USERNAME:-<cached docker login>}}"
    printf '  token:      %s\n' "${TOKEN_KIND:-not configured}"

    printf '\nChecks:\n'
    if [ "${#CHECKS[@]}" -eq 0 ]; then
        printf '  (none)\n'
    else
        local check
        for check in "${CHECKS[@]}"; do
            printf '  %s\n' "$check"
        done
    fi

    if [ "${#FINDING_CODE[@]}" -gt 0 ]; then
        printf '\nFindings:\n'
        local index severity
        for index in "${!FINDING_CODE[@]}"; do
            severity="$(printf '%s' "${FINDING_SEVERITY[$index]}" | tr '[:lower:]' '[:upper:]')"
            printf '  [%s] %s\n' "$severity" "${FINDING_CODE[$index]}"
            printf '    %s\n' "${FINDING_MESSAGE[$index]}"
            if [ -n "${FINDING_FIX[$index]}" ]; then
                printf '    fix: %s\n' "${FINDING_FIX[$index]}"
            fi
        done
    fi

    printf '\nVerdict:\n'
    if [ "$PUSH_TEST_OK" = "1" ]; then
        printf '  The diagnostic push succeeded. If the real push still fails,\n'
        printf '  the existing package access settings are the likely cause.\n'
    elif [ "$PUSH_TEST_DENIED" = "1" ]; then
        printf '  The diagnostic push was denied. The token, login account, or\n'
        printf '  namespace/package permission is the blocker.\n'
    elif [ "$EXIT_CODE" -eq 0 ]; then
        printf '  No blocking configuration, identity, or permission mismatch found.\n'
        if [ "$PUSH_TEST" != "1" ]; then
            printf '  Run again with --push-test to verify actual write access.\n'
        fi
    else
        printf '  See the findings above.\n'
    fi

    if [ "$PUSH_TEST_OK" = "1" ]; then
        printf '\n  Diagnostic package: ghcr.io/%s/ravenest-ghcr-diagnostic\n' "$IMAGE_OWNER"
        printf '  Delete it from GitHub Packages when you are done.\n'
    fi
}

main() {
    parse_args "$@"
    load_publish_config
    apply_overrides
    trap cleanup EXIT

    if require_commands; then
        if derive_image_parts; then
            add_ok "image: ${SERVER_IMAGE}"
            check_docker
            check_token
            if [ -n "$SERVER_REGISTRY_TOKEN" ] && [ "$TOKEN_USABLE" != "1" ] && [ "$NO_NETWORK" != "1" ]; then
                add_skip "namespace and package checks (token was rejected)"
            else
                check_namespace
                check_package
                check_linked_repository
            fi
            if [ -n "$SERVER_REGISTRY_TOKEN" ] && [ "$TOKEN_USABLE" != "1" ] && [ "$NO_NETWORK" != "1" ]; then
                add_skip "push test (token was rejected)"
            else
                run_push_test
            fi
        else
            check_docker
        fi
    fi

    print_report
    exit "$EXIT_CODE"
}

main "$@"
