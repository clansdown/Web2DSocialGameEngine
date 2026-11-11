#!/usr/bin/env bash
#
# install.sh — deploy the client, public game assets, and server content.
#
# The engine is generic: site-specific values (site repo, R2 bucket, API server)
# come from a gitignored .deploy file. See docs/publishing.md for setup, the
# .deploy reference, cache policy, and verification.
#
# The server image is not built or pushed here. Developers publish it with
# ./publish_engine_server_image.sh; deployments pull the already-published
# SERVER_IMAGE:SERVER_IMAGE_TAG from the registry.
#
# Usage:
#   ./install.sh [--dry-run] [--skip-build] [--skip-r2] [--skip-pages] [--skip-server]
#
# Deploy order is build -> R2 -> Pages -> API server. R2 goes before Pages so the
# new shell never references an image that has not been uploaded yet.
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

log() { printf '==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: ./install.sh [options]

Builds the client and deploys to every destination configured in .deploy.
With no destinations configured, the script fails before deploying.

Stages (run in this order):
  build   npm ci && npm run build -> client/dist
  r2      game/images, game/sounds, game/music -> R2 bucket
  pages   client/dist + overlay -> private Pages site repo, then commit and push
  server  pull SERVER_IMAGE:<tag> (when SERVER_DEPLOY_MODE=docker),
          mirror game/ -> API server content, write the runtime env,
          write/refresh the compose files, install server tools under
          ${SERVER_PATH}/tools/, then restart if configured

Options:
  --dry-run       Print deploy actions without writing to R2, the site repo,
                  or the API server. The local client build still runs unless
                  --skip-build is given.
  --skip-build    Reuse an existing client/dist (it must contain index.html).
  --skip-r2       Skip the R2 upload stage.
  --skip-pages    Skip the Pages site-repo stage.
  --skip-server   Skip the API server content/env/restart stage.
  -h, --help      Show this help.

Environment:
  DRY_RUN=1       Same as --dry-run.

Destinations are enabled by setting their keys in .deploy: SITE_REPO (Pages),
R2_BUCKET (R2), and SERVER_HOST (API server). Public origins, the server CORS
allowlist, database settings, and the prebuilt server image also come from
.deploy. Each enabled stage validates its own keys. R2 credentials are exported
only for the relevant command and are never printed.

SERVER_DEPLOY_MODE=docker (default) deploys the already-published
SERVER_IMAGE:<SERVER_IMAGE_TAG> and writes SERVER_PATH/docker-compose.yml +
compose.env. SERVER_DB_MODE=bundled (default) also writes
docker-compose.bundled.yml and an idempotent db-init SQL, running PostgreSQL in
the same compose stack. SERVER_DB_MODE=external uses the four SERVER_*_DB_URL
values instead. The systemd mode only writes the runtime env and runs
SERVER_RESTART_CMD; it requires SERVER_DB_MODE=external.

Publishing the server image is a separate developer workflow:
  ./publish_engine_server_image.sh
It reads .publish_engine, builds the image, pushes an immutable tag plus latest,
and prints the tag to pin in .deploy. install.sh never needs registry
credentials or a local Docker build.
EOF
}

# Show help before loading .deploy so a broken config cannot block --help.
for arg in "$@"; do
    case "$arg" in
        -h | --help)
            usage
            exit 0
            ;;
    esac
done

DRY_RUN="${DRY_RUN:-0}"
SKIP_BUILD=0
SKIP_R2=0
SKIP_PAGES=0
SKIP_SERVER=0

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

load_deploy() {
    [ -f .deploy ] || return 0

    local mode
    mode="$(stat -c '%a' .deploy 2>/dev/null || true)"
    case "$mode" in
        600 | 400 | '') ;;
        *) warn ".deploy is mode $mode; run: chmod 600 .deploy" ;;
    esac

    # .deploy is a private shell fragment of simple assignments.
    # shellcheck disable=SC1091
    set +u
    . ./.deploy
    set -u
}

default_config() {
    SITE_REPO="${SITE_REPO:-}"
    SITE_OVERLAY="${SITE_OVERLAY:-}"
    R2_BUCKET="${R2_BUCKET:-}"
    R2_ENDPOINT="${R2_ENDPOINT:-}"
    R2_ACCESS_KEY_ID="${R2_ACCESS_KEY_ID:-}"
    R2_SECRET_ACCESS_KEY="${R2_SECRET_ACCESS_KEY:-}"
    SERVER_HOST="${SERVER_HOST:-}"
    SERVER_PATH="${SERVER_PATH:-}"
    SERVER_SSH_KEY="${SERVER_SSH_KEY:-}"
    PUBLIC_SITE_ORIGIN="${PUBLIC_SITE_ORIGIN:-}"
    PUBLIC_API_ORIGIN="${PUBLIC_API_ORIGIN:-}"
    PUBLIC_ASSETS_ORIGIN="${PUBLIC_ASSETS_ORIGIN:-}"
    PUBLIC_WS_ORIGIN="${PUBLIC_WS_ORIGIN:-}"
    SERVER_CORS_ORIGINS="${SERVER_CORS_ORIGINS:-}"
    SERVER_ENV_PATH="${SERVER_ENV_PATH:-}"
    SERVER_RESTART_CMD="${SERVER_RESTART_CMD:-}"
    SERVER_DEPLOY_MODE="${SERVER_DEPLOY_MODE:-}"
    SERVER_DB_MODE="${SERVER_DB_MODE:-}"
    SERVER_DB_USER="${SERVER_DB_USER:-ravenest}"
    SERVER_DB_PASSWORD="${SERVER_DB_PASSWORD:-}"
    SERVER_DB_INIT_PATH="${SERVER_DB_INIT_PATH:-}"
    SERVER_COMPOSE_OVERLAY_PATH="${SERVER_COMPOSE_OVERLAY_PATH:-}"
    SERVER_GAME_DB_URL="${SERVER_GAME_DB_URL:-}"
    SERVER_CHAT_DB_URL="${SERVER_CHAT_DB_URL:-}"
    SERVER_STAFF_DB_URL="${SERVER_STAFF_DB_URL:-}"
    SERVER_INVITES_DB_URL="${SERVER_INVITES_DB_URL:-}"
    SERVER_IMAGE="${SERVER_IMAGE:-}"
    SERVER_IMAGE_TAG="${SERVER_IMAGE_TAG:-}"
    SERVER_PULL_CMD="${SERVER_PULL_CMD:-}"
    SERVER_COMPOSE_PATH="${SERVER_COMPOSE_PATH:-}"
    SERVER_COMPOSE_ENV_PATH="${SERVER_COMPOSE_ENV_PATH:-}"
    SERVER_UPLOADS_PATH="${SERVER_UPLOADS_PATH:-}"
    SERVER_UPLOADS_OWNER="${SERVER_UPLOADS_OWNER:-}"
    SERVER_ALLOW_LOOPBACK_DB_URL="${SERVER_ALLOW_LOOPBACK_DB_URL:-0}"
    CF_API_TOKEN="${CF_API_TOKEN:-}"
    CF_ZONE_ID="${CF_ZONE_ID:-}"
    SERVER_AVATAR_STORAGE="${SERVER_AVATAR_STORAGE:-local}"
    AVATAR_R2_ACCESS_KEY_ID="${AVATAR_R2_ACCESS_KEY_ID:-}"
    AVATAR_R2_SECRET_ACCESS_KEY="${AVATAR_R2_SECRET_ACCESS_KEY:-}"
    AVATAR_R2_PREFIX="${AVATAR_R2_PREFIX:-avatars/}"
    AVATAR_PUBLIC_BASE_URL="${AVATAR_PUBLIC_BASE_URL:-$PUBLIC_ASSETS_ORIGIN}"
    AVATAR_CLOUDFLARE_ZONE_ID="${AVATAR_CLOUDFLARE_ZONE_ID:-$CF_ZONE_ID}"
    AVATAR_CLOUDFLARE_API_TOKEN="${AVATAR_CLOUDFLARE_API_TOKEN:-}"
}

# The local stamp records the public origins baked into client/dist, so
# --skip-build can refuse to publish a shell built for a different .deploy.
BUILD_STAMP="client/.build-public-origins"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --dry-run) DRY_RUN=1 ;;
            --skip-build) SKIP_BUILD=1 ;;
            --skip-r2) SKIP_R2=1 ;;
            --skip-pages) SKIP_PAGES=1 ;;
            --skip-server) SKIP_SERVER=1 ;;
            -h | --help)
                usage
                exit 0
                ;;
            *)
                die "unknown argument: $1 (try --help)"
                ;;
        esac
        shift
    done

    case "$DRY_RUN" in
        1 | true | yes) DRY_RUN=1 ;;
        *) DRY_RUN=0 ;;
    esac
}

require_var() {
    local name
    for name in "$@"; do
        if [ -z "${!name:-}" ]; then
            die "$name is not set; add it to .deploy (see docs/publishing.md)"
        fi
    done
}

# Run rsync with --dry-run when requested.
rsync_run() {
    if [ "$DRY_RUN" = "1" ]; then
        rsync --dry-run "$@"
    else
        rsync "$@"
    fi
}

# Run an AWS CLI command with --dryrun when requested.
aws_run() {
    if [ "$DRY_RUN" = "1" ]; then
        aws "$@" --dryrun
    else
        aws "$@"
    fi
}

# Run SSH with the configured private key, if any.
ssh_run() {
    if [ -n "$SERVER_SSH_KEY" ]; then
        ssh -i "$SERVER_SSH_KEY" "$@"
    else
        ssh "$@"
    fi
}

# ---------------------------------------------------------------------------
# Public origin validation
# ---------------------------------------------------------------------------

# Validate one origin (scheme + host, no path). Path routing is intentionally
# not supported here: the three services live on separate hostnames.
validate_origin_value() {
    local name="$1"
    local value="$2"
    local scheme="$3"

    [ -n "$value" ] || die "$name is not set; add it to .deploy"
    case "$value" in
        *[[:space:]]*) die "$name must not contain whitespace: $value" ;;
    esac
    case "$value" in
        "$scheme"://*) ;;
        *) die "$name must start with $scheme://: $value" ;;
    esac
    case "$value" in
        *://*/* | *\?* | *#*) die "$name must be an origin (no path, query, or fragment): $value" ;;
    esac
}

# Validate a comma-separated CORS allowlist and normalise surrounding spaces.
validate_origin_list() {
    local name="$1"
    local list="$2"
    local item
    local normalized=""
    local -a items=()

    [ -n "$list" ] || die "$name is empty"
    IFS=',' read -r -a items <<< "$list"
    for item in "${items[@]}"; do
        item="${item#"${item%%[![:space:]]*}"}"
        item="${item%"${item##*[![:space:]]}"}"
        [ -n "$item" ] || die "$name contains an empty origin"
        validate_origin_value "$name entry" "$item" "https"
        normalized="${normalized:+$normalized,}$item"
    done
    SERVER_CORS_ORIGINS="$normalized"
}

# Validate everything that is baked into the build or written to the server.
preflight_public_origins() {
    if [ -n "$PUBLIC_SITE_ORIGIN" ]; then
        validate_origin_value "PUBLIC_SITE_ORIGIN" "$PUBLIC_SITE_ORIGIN" "https"
    fi
    if [ -n "$PUBLIC_API_ORIGIN" ]; then
        validate_origin_value "PUBLIC_API_ORIGIN" "$PUBLIC_API_ORIGIN" "https"
    fi
    if [ -n "$PUBLIC_ASSETS_ORIGIN" ]; then
        validate_origin_value "PUBLIC_ASSETS_ORIGIN" "$PUBLIC_ASSETS_ORIGIN" "https"
    fi
    if [ -n "$PUBLIC_WS_ORIGIN" ]; then
        validate_origin_value "PUBLIC_WS_ORIGIN" "$PUBLIC_WS_ORIGIN" "wss"
    fi

    if [ "$RUN_PAGES" = "1" ] && [ "$SKIP_BUILD" != "1" ]; then
        [ -n "$PUBLIC_API_ORIGIN" ] ||
            die "PUBLIC_API_ORIGIN is required when building the Pages client; add it to .deploy"
        [ -n "$PUBLIC_ASSETS_ORIGIN" ] ||
            die "PUBLIC_ASSETS_ORIGIN is required when building the Pages client; add it to .deploy"
    fi

    if [ "$RUN_SERVER" = "1" ]; then
        preflight_server_runtime
    fi
}

# Resolve the server deployment mode. The default is the bundled Docker stack;
# existing host-managed installs set systemd explicitly.
resolve_server_deploy_mode() {
    case "${SERVER_DEPLOY_MODE:-}" in
        "") SERVER_DEPLOY_MODE=docker ;;
        systemd | docker) ;;
        *) die "SERVER_DEPLOY_MODE must be 'systemd' or 'docker'" ;;
    esac
}

# Resolve the database topology. Bundled PostgreSQL lives in the same compose
# stack as the app; external uses the four SERVER_*_DB_URL values.
resolve_server_db_mode() {
    case "${SERVER_DB_MODE:-}" in
        "") SERVER_DB_MODE=bundled ;;
        bundled | external) ;;
        *) die "SERVER_DB_MODE must be 'bundled' or 'external'" ;;
    esac

    if [ "$SERVER_DB_MODE" = "bundled" ] && [ "$SERVER_DEPLOY_MODE" != "docker" ]; then
        die "SERVER_DB_MODE=bundled requires SERVER_DEPLOY_MODE=docker; use SERVER_DB_MODE=external for a systemd app"
    fi
}

# Every server-side path written over SSH must be absolute, safe to quote, and
# outside game/ because the content mirror uses --delete.
validate_remote_path() {
    local name="$1"
    local path="$2"

    [ -n "$path" ] || die "$name is not set"
    case "$path" in
        /*) ;;
        *) die "$name must be an absolute path: $path" ;;
    esac
    case "$path" in
        */) die "$name must not end with /: $path" ;;
        *[[:space:]\'\"\\:]* | *..*)
            die "$name contains unsupported characters: $path"
            ;;
    esac
    case "$path/" in
        "$SERVER_PATH/game/"*)
            die "$name must be outside game/ (the content mirror uses --delete)"
            ;;
    esac
}

# A DB URL is written verbatim to the runtime env file. Reject values that
# would break the line-based env format, and flag loopback hosts because a
# bridge-network container cannot reach the host through 127.0.0.1.
validate_database_url() {
    local name="$1"
    local value="$2"

    [ -n "$value" ] || die "$name is not set; add it to .deploy"
    case "$value" in
        postgresql://* | postgres://*) ;;
        *) die "$name must start with postgresql:// or postgres://" ;;
    esac
    case "$value" in
        *[[:space:]]* | *\'* | *\"* | *\\* | *'#'*)
            die "$name contains whitespace, quoting, or comment characters; percent-encode the password"
            ;;
    esac

    local host_part="${value#*://}"
    host_part="${host_part#*@}"
    host_part="${host_part%%/*}"
    host_part="${host_part%%\?*}"
    case "$host_part" in
        localhost | localhost:* | 127.0.0.1 | 127.0.0.1:* | '[::1]' | '[::1]':*)
            if [ "$SERVER_DEPLOY_MODE" = "docker" ] &&
                [ "$SERVER_ALLOW_LOOPBACK_DB_URL" != "1" ]; then
                warn "$name points at loopback; inside Docker that is the app container, not the host"
                warn "  use the host's LAN address, a compose service name, or set SERVER_ALLOW_LOOPBACK_DB_URL=1"
            fi
            ;;
    esac
}

# Validate bundled database credentials. The password is used raw by PostgreSQL
# and embedded in libpq URLs, so restricting it to URL-safe characters avoids
# double-encoding bugs between POSTGRES_PASSWORD and the URL userinfo.
validate_bundled_db_credentials() {
    [ -n "$SERVER_DB_USER" ] || die "SERVER_DB_USER is required when SERVER_DB_MODE=bundled"
    case "$SERVER_DB_USER" in
        [A-Za-z_]*)
            case "$SERVER_DB_USER" in
                *[!A-Za-z0-9_]*) die "SERVER_DB_USER must be a PostgreSQL identifier: $SERVER_DB_USER" ;;
            esac
            ;;
        *) die "SERVER_DB_USER must be a PostgreSQL identifier: $SERVER_DB_USER" ;;
    esac

    [ -n "$SERVER_DB_PASSWORD" ] ||
        die "SERVER_DB_PASSWORD is required when SERVER_DB_MODE=bundled (try: openssl rand -hex 24)"
    case "$SERVER_DB_PASSWORD" in
        *[!A-Za-z0-9._~-]*)
            die "SERVER_DB_PASSWORD must be URL-safe for bundled mode (letters, digits, . _ ~ -); try: openssl rand -hex 24"
            ;;
    esac
}

derive_bundled_database_urls() {
    SERVER_GAME_DB_URL="postgresql://${SERVER_DB_USER}:${SERVER_DB_PASSWORD}@postgres:5432/ravenest_game"
    SERVER_CHAT_DB_URL="postgresql://${SERVER_DB_USER}:${SERVER_DB_PASSWORD}@postgres:5432/ravenest_chat"
    SERVER_STAFF_DB_URL="postgresql://${SERVER_DB_USER}:${SERVER_DB_PASSWORD}@postgres:5432/ravenest_staff"
    SERVER_INVITES_DB_URL="postgresql://${SERVER_DB_USER}:${SERVER_DB_PASSWORD}@postgres:5432/ravenest_invites"
}

preflight_server_db() {
    resolve_server_db_mode

    if [ "$SERVER_DB_MODE" = "bundled" ]; then
        if [ -n "$SERVER_GAME_DB_URL$SERVER_CHAT_DB_URL$SERVER_STAFF_DB_URL$SERVER_INVITES_DB_URL" ]; then
            die "SERVER_*_DB_URL values are set while SERVER_DB_MODE=bundled; use SERVER_DB_MODE=external or clear them"
        fi
        validate_bundled_db_credentials
        derive_bundled_database_urls
        log "server database: bundled PostgreSQL (service name postgres)"
        return
    fi

    if [ -n "$SERVER_DB_PASSWORD" ]; then
        warn "SERVER_DB_PASSWORD is ignored when SERVER_DB_MODE=external (credentials come from the four URLs)"
    fi
    validate_database_url "SERVER_GAME_DB_URL" "$SERVER_GAME_DB_URL"
    validate_database_url "SERVER_CHAT_DB_URL" "$SERVER_CHAT_DB_URL"
    validate_database_url "SERVER_STAFF_DB_URL" "$SERVER_STAFF_DB_URL"
    validate_database_url "SERVER_INVITES_DB_URL" "$SERVER_INVITES_DB_URL"
}

preflight_server_runtime() {
    resolve_server_deploy_mode
    command -v ssh >/dev/null 2>&1 || die "ssh not found"
    command -v rsync >/dev/null 2>&1 || die "rsync not found"
    if [ -n "$SERVER_SSH_KEY" ] && [ ! -f "$SERVER_SSH_KEY" ]; then
        die "SERVER_SSH_KEY does not exist: $SERVER_SSH_KEY"
    fi

    SERVER_PATH="${SERVER_PATH%/}"
    [ -n "$SERVER_PATH" ] ||
        die "SERVER_PATH must name the parent of the server's game/ directory"
    SERVER_ENV_PATH="${SERVER_ENV_PATH:-${SERVER_PATH}/ravenest.env}"
    validate_remote_path "SERVER_ENV_PATH" "$SERVER_ENV_PATH"

    preflight_server_db

    if [ -z "$SERVER_CORS_ORIGINS" ]; then
        SERVER_CORS_ORIGINS="$PUBLIC_SITE_ORIGIN"
    fi
    [ -n "$SERVER_CORS_ORIGINS" ] ||
        die "SERVER_CORS_ORIGINS or PUBLIC_SITE_ORIGIN is required when SERVER_HOST is set"
    validate_origin_list "SERVER_CORS_ORIGINS" "$SERVER_CORS_ORIGINS"
    preflight_avatar_storage

    if [ "$SERVER_DEPLOY_MODE" = "docker" ]; then
        preflight_server_docker
    fi
}

# Validate the prebuilt image a Docker deployment will pull. This deliberately
# does not require registry credentials, a local Docker daemon, server/Dockerfile,
# or the image build helper: deployers only need a published image reference.
preflight_server_image_deploy() {
    require_var SERVER_IMAGE
    [ -n "$SERVER_IMAGE_TAG" ] ||
        die "SERVER_IMAGE_TAG is not set; use latest or the immutable tag printed by publish_engine_server_image.sh"

    case "$SERVER_IMAGE" in
        *://*) die "SERVER_IMAGE must be a registry/repository path without a scheme, e.g. ghcr.io/org/ravenest-server" ;;
        *[!a-z0-9._:/-]*) die "SERVER_IMAGE must be lowercase and contain only repository characters: $SERVER_IMAGE" ;;
        /* | */ | *//*) die "SERVER_IMAGE must not start/end with '/' or contain '//': $SERVER_IMAGE" ;;
    esac
    case "${SERVER_IMAGE#*/}" in
        *:*) die "SERVER_IMAGE must not include a tag; put it in SERVER_IMAGE_TAG: $SERVER_IMAGE" ;;
    esac
    case "$SERVER_IMAGE" in
        */*/*) ;;
        */*) ;;
        *) die "SERVER_IMAGE must be a full repository path, e.g. ghcr.io/org/ravenest-server" ;;
    esac
    case "$SERVER_IMAGE_TAG" in
        *[!A-Za-z0-9_.-]*) die "SERVER_IMAGE_TAG contains unsupported characters: $SERVER_IMAGE_TAG" ;;
    esac
}

preflight_server_docker() {
    preflight_server_image_deploy

    if [ -z "$SERVER_PULL_CMD" ]; then
        SERVER_PULL_CMD="docker pull '$SERVER_IMAGE:$SERVER_IMAGE_TAG'"
    fi

    SERVER_COMPOSE_PATH="${SERVER_COMPOSE_PATH:-${SERVER_PATH}/docker-compose.yml}"
    SERVER_COMPOSE_ENV_PATH="${SERVER_COMPOSE_ENV_PATH:-${SERVER_PATH}/compose.env}"
    SERVER_COMPOSE_OVERLAY_PATH="${SERVER_COMPOSE_OVERLAY_PATH:-${SERVER_PATH}/docker-compose.bundled.yml}"
    SERVER_UPLOADS_PATH="${SERVER_UPLOADS_PATH:-${SERVER_PATH}/uploads}"
    validate_remote_path "SERVER_COMPOSE_PATH" "$SERVER_COMPOSE_PATH"
    validate_remote_path "SERVER_COMPOSE_ENV_PATH" "$SERVER_COMPOSE_ENV_PATH"
    validate_remote_path "SERVER_UPLOADS_PATH" "$SERVER_UPLOADS_PATH"
    if [ -n "${SERVER_UPLOADS_OWNER:-}" ] &&
        [[ ! "$SERVER_UPLOADS_OWNER" =~ ^[A-Za-z0-9_.-]+:[A-Za-z0-9_.-]+$ ]]; then
        die "SERVER_UPLOADS_OWNER must be USER:GROUP: $SERVER_UPLOADS_OWNER"
    fi
    [ -f deploy/docker-compose.yml ] ||
        die "deploy/docker-compose.yml is missing from the engine checkout"
    [ -f tools/verify_deployment.sh ] ||
        die "tools/verify_deployment.sh is missing from the engine checkout"
    [ -f tools/restart_deployment.sh ] ||
        die "tools/restart_deployment.sh is missing from the engine checkout"
    [ -f tools/_admin_common.sh ] ||
        die "tools/_admin_common.sh is missing from the engine checkout"
    [ -f tools/_deploy_common.sh ] ||
        die "tools/_deploy_common.sh is missing from the engine checkout"
    [ -f tools/fetch_logs.sh ] ||
        die "tools/fetch_logs.sh is missing from the engine checkout"
    [ -f tools/diagnose_auth.sh ] ||
        die "tools/diagnose_auth.sh is missing from the engine checkout"
    [ -f tools/deployment_status.sh ] ||
        die "tools/deployment_status.sh is missing from the engine checkout"
    [ -f tools/staff_admin.sh ] ||
        die "tools/staff_admin.sh is missing from the engine checkout"
    [ -f tools/invite_admin.sh ] ||
        die "tools/invite_admin.sh is missing from the engine checkout"

    if [ "$SERVER_DB_MODE" = "bundled" ]; then
        SERVER_DB_INIT_PATH="${SERVER_DB_INIT_PATH:-${SERVER_PATH}/postgres/init}"
        validate_remote_path "SERVER_DB_INIT_PATH" "$SERVER_DB_INIT_PATH"
        validate_remote_path "SERVER_COMPOSE_OVERLAY_PATH" "$SERVER_COMPOSE_OVERLAY_PATH"
        [ "$SERVER_COMPOSE_OVERLAY_PATH" != "$SERVER_COMPOSE_PATH" ] ||
            die "SERVER_COMPOSE_OVERLAY_PATH must differ from SERVER_COMPOSE_PATH"
        [ -f deploy/docker-compose.bundled.yml ] ||
            die "deploy/docker-compose.bundled.yml is missing from the engine checkout"
        [ -f tools/postgres/init/01-databases.sql ] ||
            die "tools/postgres/init/01-databases.sql is missing from the engine checkout"
    fi

    if [ -z "$SERVER_RESTART_CMD" ]; then
        SERVER_RESTART_CMD="'${SERVER_PATH}/tools/restart_deployment.sh' --compose-env '${SERVER_COMPOSE_ENV_PATH}'"
    fi
}

# Avatar uploads are local in development and R2-backed in production. Validate
# the server-side configuration before touching the remote env file.
preflight_avatar_storage() {
    SERVER_AVATAR_STORAGE="$(printf '%s' "$SERVER_AVATAR_STORAGE" | tr '[:upper:]' '[:lower:]')"
    case "$SERVER_AVATAR_STORAGE" in
        local) return 0 ;;
        r2) ;;
        *) die "SERVER_AVATAR_STORAGE must be 'local' or 'r2'" ;;
    esac

    require_var R2_ENDPOINT R2_BUCKET AVATAR_R2_ACCESS_KEY_ID \
        AVATAR_R2_SECRET_ACCESS_KEY AVATAR_PUBLIC_BASE_URL
    validate_origin_value "R2_ENDPOINT" "$R2_ENDPOINT" "https"
    validate_origin_value "AVATAR_PUBLIC_BASE_URL" "$AVATAR_PUBLIC_BASE_URL" "https"

    while [ "${AVATAR_R2_PREFIX#/}" != "$AVATAR_R2_PREFIX" ]; do
        AVATAR_R2_PREFIX="${AVATAR_R2_PREFIX#/}"
    done
    while [ "${AVATAR_R2_PREFIX%/}" != "$AVATAR_R2_PREFIX" ]; do
        AVATAR_R2_PREFIX="${AVATAR_R2_PREFIX%/}"
    done
    [ -n "$AVATAR_R2_PREFIX" ] || AVATAR_R2_PREFIX="avatars"
    case "$AVATAR_R2_PREFIX" in
        *..* | *[[:space:]]* | *[?#]*)
            die "AVATAR_R2_PREFIX must be a simple object prefix"
            ;;
    esac
    AVATAR_R2_PREFIX="$AVATAR_R2_PREFIX/"

    case "$AVATAR_CLOUDFLARE_ZONE_ID:$AVATAR_CLOUDFLARE_API_TOKEN" in
        :) ;;
        :* | *:) die "AVATAR_CLOUDFLARE_ZONE_ID and AVATAR_CLOUDFLARE_API_TOKEN must be set together" ;;
    esac
}

# Set by install_remote_file so stage_server_restart knows whether the running
# service needs a restart.
REMOTE_FILE_CHANGED=0
SERVER_ENV_CHANGED=0
SERVER_COMPOSE_ENV_CHANGED=0
SERVER_COMPOSE_FILE_CHANGED=0
SERVER_COMPOSE_OVERLAY_CHANGED=0
SERVER_DB_INIT_CHANGED=0
SERVER_IMAGE_PULLED=0

# Atomically install one local file at an absolute remote path. Compares the
# SHA-256 first; backs up the old file to <path>.previous; writes <path>.new and
# moves it into place with the requested mode. Never prints file contents.
install_remote_file() {
    local source="$1"
    local remote_path="$2"
    local label="$3"
    local mode="${4:-600}"

    REMOTE_FILE_CHANGED=0
    if [ "$DRY_RUN" = "1" ]; then
        log "$label: would write $remote_path"
        return 0
    fi

    local local_hash
    local remote_hash
    local_hash="$(sha256sum "$source" | awk '{print $1}')"
    remote_hash="$(ssh_run "$SERVER_HOST" \
        "sha256sum '$remote_path' 2>/dev/null | awk '{print \$1}'" 2>/dev/null || true)"

    if [ "$local_hash" = "$remote_hash" ]; then
        log "$label: unchanged ($remote_path)"
        return 0
    fi

    local remote_dir
    remote_dir="$(dirname -- "$remote_path")"
    log "$label: writing $remote_path"
    ssh_run "$SERVER_HOST" "mkdir -p '$remote_dir'"
    ssh_run "$SERVER_HOST" \
        "if [ -f '$remote_path' ]; then cp -p '$remote_path' '$remote_path.previous'; fi"

    local -a ssh_opt=()
    if [ -n "$SERVER_SSH_KEY" ]; then
        ssh_opt=(-e "ssh -i $SERVER_SSH_KEY")
    fi
    rsync_run -a "${ssh_opt[@]}" "$source" "$SERVER_HOST:${remote_path}.new"
    ssh_run "$SERVER_HOST" \
        "chmod $mode '$remote_path.new' && mv '$remote_path.new' '$remote_path'"
    REMOTE_FILE_CHANGED=1
}

# Refuse to reuse a client/dist built for different public origins.
check_build_stamp() {
    local expected
    local actual

    [ -f "$BUILD_STAMP" ] ||
        die "client/dist has no public-origin stamp; rebuild without --skip-build"
    expected="$(printf 'api=%s\nassets=%s\nws=%s\n' \
        "$PUBLIC_API_ORIGIN" "$PUBLIC_ASSETS_ORIGIN" "$PUBLIC_WS_ORIGIN")"
    actual="$(cat "$BUILD_STAMP")"
    if [ "$expected" != "$actual" ]; then
        die "client/dist public origins do not match .deploy; rebuild without --skip-build"
    fi
}

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------

preflight() {
    local private_path
    for private_path in .deploy .publish_engine backup.sh push_to_production.sh; do
        if git -C "$SCRIPT_DIR" ls-files --error-unmatch "$private_path" >/dev/null 2>&1; then
            die "$private_path is tracked by git; remove it from the index and keep it private"
        fi
    done

    [ -d client ] || die "client/ not found; run install.sh from the engine repository"

    preflight_git
    preflight_public_origins
}

preflight_git() {
    git -C "$SCRIPT_DIR" rev-parse --git-dir >/dev/null 2>&1 || return 0

    local tracked_build
    tracked_build="$(git -C "$SCRIPT_DIR" ls-files -- \
        'server/build/*' \
        'server/build-*/*' \
        'game_balance_analyzer/build/*' 2>/dev/null || true)"
    if [ -n "$tracked_build" ]; then
        printf '%s\n' "$tracked_build" >&2
        die "build output is tracked by git; remove it from the index before publishing"
    fi

    git -C "$SCRIPT_DIR" rev-parse --verify HEAD >/dev/null 2>&1 || return 0

    # GitHub rejects files over 100 MiB. Warn well before that so a future
    # accidental binary is caught by the publisher rather than by the push.
    local large
    large="$(git -C "$SCRIPT_DIR" ls-tree -r -l HEAD 2>/dev/null | awk '$4 > 52428800 { print }' || true)"
    if [ -n "$large" ]; then
        warn "tracked files larger than 50 MiB (GitHub rejects files over 100 MiB):"
        printf '%s\n' "$large" | while IFS= read -r line; do
            printf '  %s\n' "$line" >&2
        done
        if printf '%s\n' "$large" | awk '{ if ($4 > 104857600) bad = 1 } END { exit bad ? 0 : 1 }'; then
            die "tracked files exceed GitHub's 100 MiB limit; remove them before pushing"
        fi
    fi
}

# ---------------------------------------------------------------------------
# Stage 1 — build
# ---------------------------------------------------------------------------

stage_build() {
    if [ "$SKIP_BUILD" = "1" ]; then
        if [ "$RUN_PAGES" = "1" ]; then
            check_build_stamp
        fi
        log "build: skipped"
        return
    fi

    command -v npm >/dev/null 2>&1 || die "npm not found"
    [ -d client/SimpleGame/ui ] ||
        die "client/SimpleGame/ui is missing; clone SimpleGame into client/SimpleGame (see README.md)"
    [ -f client/package-lock.json ] || die "client/package-lock.json is missing; npm ci requires it"

    # Vite bakes these into the bundle. Empty values preserve the same-origin
    # dev proxy/reverse-proxy behaviour; production Pages builds require the
    # API and asset origins (validated in preflight_public_origins).
    export VITE_API_BASE_URL="$PUBLIC_API_ORIGIN"
    export VITE_ASSETS_BASE_URL="$PUBLIC_ASSETS_ORIGIN"
    export VITE_WS_BASE_URL="$PUBLIC_WS_ORIGIN"
    log "build: api=${PUBLIC_API_ORIGIN:-same-origin} assets=${PUBLIC_ASSETS_ORIGIN:-same-origin} ws=${PUBLIC_WS_ORIGIN:-derived}"

    rm -f "$BUILD_STAMP"

    log "build: npm ci"
    npm --prefix client ci --no-audit --no-fund

    log "build: npm run build"
    npm --prefix client run build

    [ -f client/dist/index.html ] || die "build did not produce client/dist/index.html"
    printf 'api=%s\nassets=%s\nws=%s\n' \
        "$PUBLIC_API_ORIGIN" "$PUBLIC_ASSETS_ORIGIN" "$PUBLIC_WS_ORIGIN" > "$BUILD_STAMP"
    log "build: client/dist/index.html present"
}

# ---------------------------------------------------------------------------
# Stage 2 — R2 public assets
# ---------------------------------------------------------------------------

stage_r2() {
    require_var R2_BUCKET R2_ENDPOINT R2_ACCESS_KEY_ID R2_SECRET_ACCESS_KEY
    command -v aws >/dev/null 2>&1 || die "aws CLI not found (install AWS CLI v2)"
    [ -d game ] || die "game/ not found; the R2 stage uploads from game/images, game/sounds, and game/music"
    case "$R2_ENDPOINT" in
        https://*) ;;
        *) die "R2_ENDPOINT must start with https://" ;;
    esac

    export AWS_ACCESS_KEY_ID="$R2_ACCESS_KEY_ID"
    export AWS_SECRET_ACCESS_KEY="$R2_SECRET_ACCESS_KEY"
    export AWS_DEFAULT_REGION="${AWS_DEFAULT_REGION:-auto}"
    # Recent AWS CLI v2 releases add flexible checksums that R2 may reject.
    # Keep checksums to the operations that require them.
    export AWS_REQUEST_CHECKSUM_CALCULATION="${AWS_REQUEST_CHECKSUM_CALCULATION:-when_required}"
    export AWS_RESPONSE_CHECKSUM_VALIDATION="${AWS_RESPONSE_CHECKSUM_VALIDATION:-when_required}"
    unset AWS_PROFILE 2>/dev/null || true

    local dir found=0
    for dir in images sounds music; do
        if [ ! -d "game/$dir" ]; then
            continue
        fi

        # An empty source tree would make --delete wipe the remote prefix.
        # Skip it instead; this is usually a missing content checkout.
        if ! find "game/$dir" -type f \
            ! -name '*.xcf' ! -name '*.md' ! -name '.gitkeep' \
            -print -quit | grep -q .; then
            warn "r2: skipping game/$dir (no publishable files)"
            continue
        fi

        found=1
        log "r2: game/$dir -> s3://$R2_BUCKET/$dir"
        aws_run s3 sync "game/$dir" "s3://$R2_BUCKET/$dir" \
            --delete \
            --endpoint-url "$R2_ENDPOINT" \
            --exclude '*.xcf' \
            --exclude '*.md' \
            --exclude '.gitkeep'

        # Older AWS CLI versions guess .avif as application/octet-stream. A
        # separate cp pass (not sync) forces the correct content type; sync
        # would skip unchanged objects because size and mtime already match.
        if find "game/$dir" -type f -name '*.avif' -print -quit | grep -q .; then
            log "r2: setting image/avif content type under game/$dir"
            aws_run s3 cp "game/$dir" "s3://$R2_BUCKET/$dir" \
                --recursive \
                --exclude '*' \
                --include '*.avif' \
                --content-type image/avif \
                --endpoint-url "$R2_ENDPOINT"
        fi
    done

    if [ "$found" = "0" ]; then
        warn "no game/images, game/sounds, or game/music directories; R2 stage did nothing"
    fi

    unset AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY AWS_DEFAULT_REGION
    unset AWS_REQUEST_CHECKSUM_CALCULATION AWS_RESPONSE_CHECKSUM_VALIDATION
}

# ---------------------------------------------------------------------------
# Stage 3 — Pages site repo
# ---------------------------------------------------------------------------

stage_pages() {
    require_var SITE_REPO
    command -v git >/dev/null 2>&1 || die "git not found"
    command -v rsync >/dev/null 2>&1 || die "rsync not found"

    [ -d "$SITE_REPO" ] || die "SITE_REPO does not exist: $SITE_REPO"
    SITE_REPO="$(cd -- "$SITE_REPO" && pwd -P)"
    git -C "$SITE_REPO" rev-parse --git-dir >/dev/null 2>&1 ||
        die "SITE_REPO is not a git repository: $SITE_REPO"
    if [ "$(git -C "$SITE_REPO" rev-parse --is-bare-repository 2>/dev/null || echo unknown)" = "true" ]; then
        die "SITE_REPO is a bare repository; it must have a work tree: $SITE_REPO"
    fi
    [ "$SITE_REPO" != "$SCRIPT_DIR" ] || die "SITE_REPO must not be the engine repository"
    [ "$SITE_REPO" != "/" ] || die "SITE_REPO must not be the filesystem root"
    if [ -n "${HOME:-}" ] && [ "$SITE_REPO" = "$HOME" ]; then
        die "SITE_REPO must not be your home directory"
    fi
    for marker in package.json src client server; do
        if [ -e "$SITE_REPO/$marker" ]; then
            die "SITE_REPO looks like a source repository, not a generated Pages site: $SITE_REPO"
        fi
    done
    if [ -n "$(find "$SITE_REPO" -maxdepth 1 -mindepth 1 ! -name '.git' -print -quit)" ] &&
        [ ! -e "$SITE_REPO/index.html" ]; then
        die "SITE_REPO is not empty and has no index.html; refusing to run --delete against it"
    fi

    # The sync uses --delete. Never let the site repo be, contain, or live
    # inside the engine checkout.
    case "$SITE_REPO/" in
        "$SCRIPT_DIR"/*) die "SITE_REPO must be outside the engine repository: $SITE_REPO" ;;
    esac
    case "$SCRIPT_DIR/" in
        "$SITE_REPO"/*) die "SITE_REPO must not contain the engine repository: $SITE_REPO" ;;
    esac

    [ -f client/dist/index.html ] || die "client/dist/index.html is missing; run without --skip-build first"

    local overlay_dir=""
    if [ -z "$SITE_OVERLAY" ]; then
        warn "SITE_OVERLAY is not set; the pages repo will publish without _headers/robots/favicon"
    fi
    if [ -n "$SITE_OVERLAY" ]; then
        [ -d "$SITE_OVERLAY" ] || die "SITE_OVERLAY does not exist: $SITE_OVERLAY"
        overlay_dir="$(cd -- "$SITE_OVERLAY" && pwd -P)"
        [ "$overlay_dir" != "$SCRIPT_DIR" ] || die "SITE_OVERLAY must not be the engine repository"
        [ "$overlay_dir" != "$SITE_REPO" ] || die "SITE_OVERLAY must be separate from SITE_REPO"
        case "$overlay_dir/" in
            "$SITE_REPO"/*) die "SITE_OVERLAY must not live inside SITE_REPO (the Pages sync uses --delete)" ;;
        esac
        case "$SITE_REPO/" in
            "$overlay_dir"/*) die "SITE_REPO must not live inside SITE_OVERLAY" ;;
        esac
    fi

    log "pages: client/dist/ -> $SITE_REPO/"
    rsync_run -a --delete --exclude=.git client/dist/ "$SITE_REPO/"

    if [ -n "$overlay_dir" ]; then
        log "pages: overlay $overlay_dir/ -> $SITE_REPO/"
        rsync_run -a --exclude=.git "$overlay_dir/" "$SITE_REPO/"
    fi

    if [ "$DRY_RUN" = "1" ]; then
        log "pages: dry run; current site repo status follows (nothing staged or pushed)"
        git -C "$SITE_REPO" status --short || true
        return
    fi

    git -C "$SITE_REPO" add -A

    local have_head=0
    if git -C "$SITE_REPO" rev-parse --verify HEAD >/dev/null 2>&1; then
        have_head=1
    fi

    if [ "$have_head" = "1" ] && git -C "$SITE_REPO" diff --cached --quiet; then
        log "pages: no build changes to commit"
    else
        local message
        message="deploy $(date -u +%Y-%m-%dT%H:%M:%SZ) engine@${ENGINE_SHA}${ENGINE_DIRTY}"
        log "pages: committing: $message"
        git -C "$SITE_REPO" commit -m "$message"
    fi

    # Always push: this also delivers a commit left behind by an earlier
    # failed push, even when the current build produced no changes. A fresh
    # clone of an empty repository has no resolvable '@{u}' yet (Git shows
    # origin/master as [gone]), so push the current branch with -u instead of
    # refusing to touch the repository.
    if git -C "$SITE_REPO" rev-parse --abbrev-ref --symbolic-full-name '@{u}' >/dev/null 2>&1; then
        git -C "$SITE_REPO" push
    else
        local remote=""
        if git -C "$SITE_REPO" remote get-url origin >/dev/null 2>&1; then
            remote=origin
        else
            local remotes
            remotes="$(git -C "$SITE_REPO" remote)"
            remote="${remotes%%$'\n'*}"
        fi
        [ -n "$remote" ] || die "SITE_REPO has no git remote; add a remote and push manually"

        local branch
        branch="$(git -C "$SITE_REPO" symbolic-ref --quiet --short HEAD || true)"
        [ -n "$branch" ] || die "SITE_REPO is not on a branch; check out a branch before publishing"

        log "pages: no upstream; pushing $remote/$branch and setting upstream"
        git -C "$SITE_REPO" push -u "$remote" "$branch"
    fi
}

# ---------------------------------------------------------------------------
# Stage 4 — pull the prebuilt server image (Docker deployments)
# ---------------------------------------------------------------------------

stage_server_pull() {
    [ "$SERVER_DEPLOY_MODE" = "docker" ] || return 0
    [ -n "$SERVER_PULL_CMD" ] || return 0

    if [ "$DRY_RUN" = "1" ]; then
        log "server image: would pull ${SERVER_IMAGE}:${SERVER_IMAGE_TAG}"
        return
    fi

    log "server image: pulling ${SERVER_IMAGE}:${SERVER_IMAGE_TAG}"
    ssh_run "$SERVER_HOST" "$SERVER_PULL_CMD"
    SERVER_IMAGE_PULLED=1
}

# ---------------------------------------------------------------------------
# Stage 5 — API server content mirror
# ---------------------------------------------------------------------------

stage_server_content() {
    require_var SERVER_HOST SERVER_PATH
    command -v rsync >/dev/null 2>&1 || die "rsync not found"
    [ -d game ] || die "game/ not found"

    # The mirror uses --delete. An empty tree would wipe the server's game/
    # directory; this is usually a missing content checkout.
    if ! find game -type f \
        ! -path 'game/uploads/*' \
        ! -path 'game/.git/*' \
        ! -name '*.db' ! -name '*.xcf' ! -name '*.md' ! -name '.gitkeep' \
        -print -quit | grep -q .; then
        die "game/ has no publishable files; refusing to mirror with --delete"
    fi

    if [ -n "$SERVER_SSH_KEY" ] && [ ! -f "$SERVER_SSH_KEY" ]; then
        die "SERVER_SSH_KEY does not exist: $SERVER_SSH_KEY"
    fi

    SERVER_PATH="${SERVER_PATH%/}"
    [ -n "$SERVER_PATH" ] || die "SERVER_PATH must name the parent of the server's game/ directory"

    local -a ssh_opt=()
    if [ -n "$SERVER_SSH_KEY" ]; then
        ssh_opt=(-e "ssh -i $SERVER_SSH_KEY")
    fi

    log "server: game/ -> $SERVER_HOST:${SERVER_PATH}/game/"
    if [ "${#ssh_opt[@]}" -gt 0 ]; then
        rsync_run -a --delete "${ssh_opt[@]}" \
            --exclude='*.db' \
            --exclude='*.xcf' \
            --exclude='.git/' \
            --exclude='uploads/' \
            --exclude='*.md' \
            --exclude='.gitkeep' \
            game/ "$SERVER_HOST:${SERVER_PATH}/game/"
    else
        rsync_run -a --delete \
            --exclude='*.db' \
            --exclude='*.xcf' \
            --exclude='.git/' \
            --exclude='uploads/' \
            --exclude='*.md' \
            --exclude='.gitkeep' \
            game/ "$SERVER_HOST:${SERVER_PATH}/game/"
    fi
}

# Write the API server runtime env. The file is generated from .deploy (the
# single source) and installed atomically. Database URLs and secrets are never
# printed.
stage_server_runtime() {
    require_var SERVER_HOST SERVER_CORS_ORIGINS SERVER_ENV_PATH
    require_var SERVER_GAME_DB_URL SERVER_CHAT_DB_URL SERVER_STAFF_DB_URL SERVER_INVITES_DB_URL

    local temp
    temp="$(mktemp "${TMPDIR:-/tmp}/ravenest-env.XXXXXX")"
    {
        printf '# Generated by install.sh from .deploy; do not edit.\n'
        printf 'RAVENEST_GAME_DB_URL=%s\n' "$SERVER_GAME_DB_URL"
        printf 'RAVENEST_CHAT_DB_URL=%s\n' "$SERVER_CHAT_DB_URL"
        printf 'RAVENEST_STAFF_DB_URL=%s\n' "$SERVER_STAFF_DB_URL"
        printf 'RAVENEST_INVITES_DB_URL=%s\n' "$SERVER_INVITES_DB_URL"
        printf 'RAVENEST_CORS_ORIGINS=%s\n' "$SERVER_CORS_ORIGINS"
        printf 'RAVENEST_AVATAR_STORAGE=%s\n' "$SERVER_AVATAR_STORAGE"
        if [ "$SERVER_AVATAR_STORAGE" = "r2" ]; then
            printf 'RAVENEST_R2_ENDPOINT=%s\n' "$R2_ENDPOINT"
            printf 'RAVENEST_R2_BUCKET=%s\n' "$R2_BUCKET"
            printf 'RAVENEST_R2_REGION=auto\n'
            printf 'RAVENEST_R2_ACCESS_KEY_ID=%s\n' "$AVATAR_R2_ACCESS_KEY_ID"
            printf 'RAVENEST_R2_SECRET_ACCESS_KEY=%s\n' "$AVATAR_R2_SECRET_ACCESS_KEY"
            printf 'RAVENEST_AVATAR_R2_PREFIX=%s\n' "$AVATAR_R2_PREFIX"
            printf 'RAVENEST_AVATAR_PUBLIC_BASE_URL=%s\n' "$AVATAR_PUBLIC_BASE_URL"
            if [ -n "$AVATAR_CLOUDFLARE_ZONE_ID" ]; then
                printf 'RAVENEST_CLOUDFLARE_ZONE_ID=%s\n' "$AVATAR_CLOUDFLARE_ZONE_ID"
                printf 'RAVENEST_CLOUDFLARE_API_TOKEN=%s\n' "$AVATAR_CLOUDFLARE_API_TOKEN"
            fi
        fi
    } > "$temp"
    chmod 600 "$temp"

    if [ "$DRY_RUN" = "1" ]; then
        log "server: database URLs configured (redacted)"
        log "server: RAVENEST_CORS_ORIGINS=$SERVER_CORS_ORIGINS"
        log "server: RAVENEST_AVATAR_STORAGE=$SERVER_AVATAR_STORAGE"
        if [ "$SERVER_AVATAR_STORAGE" = "r2" ]; then
            log "server: RAVENEST_AVATAR_PUBLIC_BASE_URL=$AVATAR_PUBLIC_BASE_URL"
            log "server: RAVENEST_AVATAR_R2_PREFIX=$AVATAR_R2_PREFIX"
        fi
    fi

    install_remote_file "$temp" "$SERVER_ENV_PATH" "server env" 600
    SERVER_ENV_CHANGED="$REMOTE_FILE_CHANGED"
    rm -f "$temp"
}

# Write the compose file and compose interpolation env. The image tag lives in
# the interpolation env, so a new image triggers a restart even when the
# runtime env is unchanged.
stage_server_compose() {
    [ "$SERVER_DEPLOY_MODE" = "docker" ] || return 0

    if [ "$DRY_RUN" != "1" ]; then
        ssh_run "$SERVER_HOST" "mkdir -p '$SERVER_UPLOADS_PATH'"

        local uploads_target="${SERVER_UPLOADS_OWNER:-10001:10001}"
        local uploads_chowned=0

        # Direct chown works when the SSH user is root or otherwise permitted.
        if ssh_run "$SERVER_HOST" \
            "chown -R '$uploads_target' '$SERVER_UPLOADS_PATH' 2>/dev/null"; then
            uploads_chowned=1
        fi

        # Docker deployments can use the already-pulled app image as a root
        # helper to chown the bind mount without requiring sudo on the host.
        if [ "$uploads_chowned" != "1" ] && [ "$SERVER_DEPLOY_MODE" = "docker" ]; then
            log "server: setting uploads ownership with a helper container"
            if ssh_run "$SERVER_HOST" \
                "docker run --rm --user 0 --entrypoint /bin/sh -v '$SERVER_UPLOADS_PATH:/uploads' '$SERVER_IMAGE:$SERVER_IMAGE_TAG' -c 'chown -R \"\$1\" /uploads' sh '$uploads_target'"; then
                uploads_chowned=1
            fi
        fi

        if [ "$uploads_chowned" != "1" ]; then
            warn "server: could not chown $SERVER_UPLOADS_PATH to $uploads_target"
            warn "  run: sudo chown -R $uploads_target $SERVER_UPLOADS_PATH"
            warn "  (or set SERVER_UPLOADS_OWNER if the container uid differs)"
        fi

        local uploads_owner
        uploads_owner="$(ssh_run "$SERVER_HOST" \
            "stat -c '%u:%g' '$SERVER_UPLOADS_PATH' 2>/dev/null" 2>/dev/null || true)"
        if [ -n "$uploads_owner" ] && [ "$uploads_owner" != "$uploads_target" ] &&
            [ -z "$SERVER_UPLOADS_OWNER" ]; then
            warn "server: $SERVER_UPLOADS_PATH is owned by $uploads_owner; expected $uploads_target"
            warn "  (or chown it manually) if local avatar uploads must work"
        fi
    fi

    local temp
    temp="$(mktemp "${TMPDIR:-/tmp}/ravenest-compose-env.XXXXXX")"
    {
        printf '# Generated by install.sh from .deploy; do not edit.\n'
        printf 'RAVENEST_IMAGE=%s:%s\n' "$SERVER_IMAGE" "$SERVER_IMAGE_TAG"
        printf 'RAVENEST_ENV_FILE=%s\n' "$SERVER_ENV_PATH"
        printf 'RAVENEST_GAME_PATH=%s\n' "${SERVER_PATH}/game"
        printf 'RAVENEST_UPLOADS_PATH=%s\n' "$SERVER_UPLOADS_PATH"
        printf 'RAVENEST_COMPOSE_PATH=%s\n' "$SERVER_COMPOSE_PATH"
        if [ "$SERVER_DB_MODE" = "bundled" ]; then
            printf 'RAVENEST_DB_USER=%s\n' "$SERVER_DB_USER"
            printf 'RAVENEST_DB_PASSWORD=%s\n' "$SERVER_DB_PASSWORD"
            printf 'RAVENEST_DB_INIT_PATH=%s\n' "$SERVER_DB_INIT_PATH"
            printf 'RAVENEST_COMPOSE_OVERLAY_PATH=%s\n' "$SERVER_COMPOSE_OVERLAY_PATH"
        fi
    } > "$temp"
    chmod 600 "$temp"

    install_remote_file "deploy/docker-compose.yml" "$SERVER_COMPOSE_PATH" \
        "server compose" 644
    SERVER_COMPOSE_FILE_CHANGED="$REMOTE_FILE_CHANGED"

    if [ "$SERVER_DB_MODE" = "bundled" ]; then
        install_remote_file "deploy/docker-compose.bundled.yml" \
            "$SERVER_COMPOSE_OVERLAY_PATH" "server compose bundled overlay" 644
        SERVER_COMPOSE_OVERLAY_CHANGED="$REMOTE_FILE_CHANGED"
        if [ "$SERVER_COMPOSE_OVERLAY_CHANGED" = "1" ]; then
            SERVER_COMPOSE_FILE_CHANGED=1
        fi
        install_remote_file "tools/postgres/init/01-databases.sql" \
            "${SERVER_DB_INIT_PATH}/01-databases.sql" "server db init" 644
        SERVER_DB_INIT_CHANGED="$REMOTE_FILE_CHANGED"
        if [ "$SERVER_DB_INIT_CHANGED" = "1" ]; then
            SERVER_COMPOSE_FILE_CHANGED=1
        fi
    fi

    install_remote_file "$temp" "$SERVER_COMPOSE_ENV_PATH" \
        "server compose env" 600
    SERVER_COMPOSE_ENV_CHANGED="$REMOTE_FILE_CHANGED"
    rm -f "$temp"
}

# Install host-side operational tools under ${SERVER_PATH}/tools/:
#   - read-only deployment verifier (not run automatically)
#   - compose/restart helper (used as the default SERVER_RESTART_CMD)
#   - log fetcher, auth diagnostic, and deployment status
#   - staff_admin / invite_admin wrappers that exec the image binaries
stage_server_tools() {
    [ "$SERVER_DEPLOY_MODE" = "docker" ] || return 0

    install_remote_file "tools/verify_deployment.sh" \
        "${SERVER_PATH}/tools/verify_deployment.sh" \
        "server verify script" 755
    install_remote_file "tools/restart_deployment.sh" \
        "${SERVER_PATH}/tools/restart_deployment.sh" \
        "server restart script" 755
    install_remote_file "tools/_admin_common.sh" \
        "${SERVER_PATH}/tools/_admin_common.sh" \
        "server admin helper" 755
    install_remote_file "tools/_deploy_common.sh" \
        "${SERVER_PATH}/tools/_deploy_common.sh" \
        "server deployment helper" 755
    install_remote_file "tools/fetch_logs.sh" \
        "${SERVER_PATH}/tools/fetch_logs.sh" \
        "server log fetcher" 755
    install_remote_file "tools/diagnose_auth.sh" \
        "${SERVER_PATH}/tools/diagnose_auth.sh" \
        "server auth diagnostic" 755
    install_remote_file "tools/deployment_status.sh" \
        "${SERVER_PATH}/tools/deployment_status.sh" \
        "server deployment status" 755
    install_remote_file "tools/staff_admin.sh" \
        "${SERVER_PATH}/tools/staff_admin.sh" \
        "server staff admin wrapper" 755
    install_remote_file "tools/invite_admin.sh" \
        "${SERVER_PATH}/tools/invite_admin.sh" \
        "server invite admin wrapper" 755
    log "server tools: ${SERVER_PATH}/tools/"
}

# Restart only when something the running service reads changed. The compose
# env contains the image tag, so a new release restarts even if the runtime env
# is byte-identical.
stage_server_restart() {
    if [ "$DRY_RUN" = "1" ]; then
        if [ -n "$SERVER_RESTART_CMD" ]; then
            log "server: would restart: $SERVER_RESTART_CMD"
        else
            log "server: no restart command configured"
        fi
        return
    fi

    local should_restart=0
    if [ "${SERVER_IMAGE_PULLED:-0}" = "1" ]; then
        should_restart=1
    fi
    if [ "${SERVER_ENV_CHANGED:-0}" = "1" ]; then
        should_restart=1
    fi
    if [ "$SERVER_DEPLOY_MODE" = "docker" ] &&
        { [ "${SERVER_COMPOSE_ENV_CHANGED:-0}" = "1" ] ||
            [ "${SERVER_COMPOSE_FILE_CHANGED:-0}" = "1" ]; }; then
        should_restart=1
    fi

    if [ "$should_restart" != "1" ]; then
        log "server: no runtime changes; not restarting"
        return
    fi

    if [ -z "$SERVER_RESTART_CMD" ]; then
        printf 'note: API server files were updated but the server was not restarted.\n' >&2
        printf '      Restart it when convenient to apply the new env/image.\n' >&2
        printf '      Set SERVER_RESTART_CMD in .deploy to automate this.\n' >&2
        return
    fi

    log "server: restarting API service"
    if ! ssh_run "$SERVER_HOST" "$SERVER_RESTART_CMD"; then
        warn "server: restart failed; restoring previous files"
        local restore=""
        if [ "${SERVER_ENV_CHANGED:-0}" = "1" ]; then
            restore="${restore}if [ -f '$SERVER_ENV_PATH.previous' ]; then mv '$SERVER_ENV_PATH.previous' '$SERVER_ENV_PATH'; fi; "
        fi
        if [ "$SERVER_DEPLOY_MODE" = "docker" ]; then
            if [ "${SERVER_COMPOSE_ENV_CHANGED:-0}" = "1" ]; then
                restore="${restore}if [ -f '$SERVER_COMPOSE_ENV_PATH.previous' ]; then mv '$SERVER_COMPOSE_ENV_PATH.previous' '$SERVER_COMPOSE_ENV_PATH'; fi; "
            fi
            if [ "${SERVER_COMPOSE_FILE_CHANGED:-0}" = "1" ]; then
                restore="${restore}if [ -f '$SERVER_COMPOSE_PATH.previous' ]; then mv '$SERVER_COMPOSE_PATH.previous' '$SERVER_COMPOSE_PATH'; fi; "
            fi
            if [ "${SERVER_COMPOSE_OVERLAY_CHANGED:-0}" = "1" ]; then
                restore="${restore}if [ -f '$SERVER_COMPOSE_OVERLAY_PATH.previous' ]; then mv '$SERVER_COMPOSE_OVERLAY_PATH.previous' '$SERVER_COMPOSE_OVERLAY_PATH'; fi; "
            fi
            if [ "${SERVER_DB_INIT_CHANGED:-0}" = "1" ]; then
                restore="${restore}if [ -f '${SERVER_DB_INIT_PATH}/01-databases.sql.previous' ]; then mv '${SERVER_DB_INIT_PATH}/01-databases.sql.previous' '${SERVER_DB_INIT_PATH}/01-databases.sql'; fi; "
            fi
        fi
        if [ -n "$restore" ]; then
            ssh_run "$SERVER_HOST" "$restore" || true
        fi
        ssh_run "$SERVER_HOST" "$SERVER_RESTART_CMD" || true
        die "server restart failed after writing $SERVER_ENV_PATH"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

load_deploy
default_config
parse_args "$@"

if [ -z "$SITE_REPO" ] && [ -z "$R2_BUCKET" ] && [ -z "$SERVER_HOST" ]; then
    die "no publish destination configured; create .deploy or set SITE_REPO, R2_BUCKET, or SERVER_HOST"
fi

ENGINE_SHA="$(git -C "$SCRIPT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
ENGINE_DIRTY=""
if [ -n "$(git -C "$SCRIPT_DIR" status --porcelain 2>/dev/null || true)" ]; then
    ENGINE_DIRTY="-dirty"
fi

RUN_R2=1
RUN_PAGES=1
RUN_SERVER=1
if [ -z "$R2_BUCKET" ]; then RUN_R2=0; fi
if [ -z "$SITE_REPO" ]; then RUN_PAGES=0; fi
if [ -z "$SERVER_HOST" ]; then RUN_SERVER=0; fi
if [ "$SKIP_R2" = "1" ]; then RUN_R2=0; fi
if [ "$SKIP_PAGES" = "1" ]; then RUN_PAGES=0; fi
if [ "$SKIP_SERVER" = "1" ]; then RUN_SERVER=0; fi

log "engine: $SCRIPT_DIR @ ${ENGINE_SHA}${ENGINE_DIRTY}"
if [ "$DRY_RUN" = "1" ]; then
    log "dry run: no writes to R2, the site repo, or the API server"
fi

preflight

stage_build

if [ "$RUN_R2" = "1" ]; then
    stage_r2
else
    log "r2: skipped (not configured or --skip-r2)"
fi

if [ "$RUN_PAGES" = "1" ]; then
    stage_pages
else
    log "pages: skipped (not configured or --skip-pages)"
fi

if [ "$RUN_SERVER" = "1" ]; then
    stage_server_pull
    stage_server_content
    stage_server_runtime
    stage_server_compose
    stage_server_tools
    stage_server_restart
else
    log "server: skipped (not configured or --skip-server)"
fi

log "done"
