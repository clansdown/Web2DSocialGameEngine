#!/usr/bin/env bash
#
# publish_engine_server_image.sh — build and push the engine server image.
#
# This is the developer-only publishing path. It never touches a deployment,
# the client, R2, Pages, or game content. Deployments use tools/install.sh and pull
# the already-published SERVER_IMAGE:SERVER_IMAGE_TAG.
#
# Configuration defaults to .publish_engine (gitignored), with environment and
# CLI overrides. The working tree must be clean: publishing a dirty tree would
# create an image that cannot be reproduced from a commit.
#
# Usage:
#   ./publish_engine_server_image.sh [options]
#
# Options:
#   --config FILE     Read publishing values from FILE (default: .publish_engine)
#   --image IMAGE     Override SERVER_IMAGE
#   --tag TAG         Override the derived immutable tag
#   --platform P      Override SERVER_PLATFORM (default: linux/amd64)
#   --no-latest       Do not also push the moving :latest tag
#   --dry-run         Print actions without building, logging in, or pushing
#   -h, --help        Show this help
#
# Published tags:
#   <SERVER_IMAGE>:<UTC timestamp>-<git sha>
#   <SERVER_IMAGE>:latest                 (unless --no-latest)
#
# The immutable tag is printed as SERVER_IMAGE_TAG=<tag> for pinning in .deploy.

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
Usage: ./publish_engine_server_image.sh [options]

Builds and pushes the engine server image. This is the developer publishing
path; it does not deploy anything.

Options:
  --config FILE     Read publishing values from FILE (default: .publish_engine)
  --image IMAGE     Override SERVER_IMAGE
  --tag TAG         Override the derived immutable tag
  --platform P      Override SERVER_PLATFORM (default: linux/amd64)
  --no-latest       Do not also push the moving :latest tag
  --dry-run         Print actions without building, logging in, or pushing
  -h, --help        Show this help

Configuration (.publish_engine):
  SERVER_IMAGE=ghcr.io/<owner>/<lowercase-package>
  SERVER_REGISTRY_USERNAME=<registry user>
  SERVER_REGISTRY_TOKEN=<PAT with write:packages>
  SERVER_PLATFORM=linux/amd64

The image repository must not include a tag. Tags are derived from the clean
commit as <UTC timestamp>-<git sha>. The immutable tag is printed for pinning
in .deploy. The moving :latest tag is pushed unless --no-latest is used.

Environment variables may be used instead of the file. Credentials are passed
to docker login through stdin and are never printed.
EOF
}

CONFIG_FILE=".publish_engine"
OPT_IMAGE=""
OPT_TAG=""
OPT_PLATFORM=""
PUSH_LATEST=1
DRY_RUN=0

SERVER_IMAGE=""
SERVER_REGISTRY_USERNAME=""
SERVER_REGISTRY_TOKEN=""
SERVER_PLATFORM=""
SERVER_IMAGE_TAG=""
ENGINE_SHA=""
TAG=""
REGISTRY=""

for arg in "$@"; do
    case "$arg" in
        -h | --help)
            usage
            exit 0
            ;;
    esac
done

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --config)
                [ "$#" -ge 2 ] || die "--config requires a file"
                CONFIG_FILE="$2"
                shift 2
                ;;
            --image)
                [ "$#" -ge 2 ] || die "--image requires a value"
                OPT_IMAGE="$2"
                shift 2
                ;;
            --tag)
                [ "$#" -ge 2 ] || die "--tag requires a value"
                OPT_TAG="$2"
                shift 2
                ;;
            --platform)
                [ "$#" -ge 2 ] || die "--platform requires a value"
                OPT_PLATFORM="$2"
                shift 2
                ;;
            --no-latest)
                PUSH_LATEST=0
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
                die "unknown argument: $1 (try --help)"
                ;;
        esac
    done
}

load_publish_config() {
    if [ ! -f "$CONFIG_FILE" ]; then
        warn "no $CONFIG_FILE; using environment/CLI values"
        return 0
    fi

    local mode
    mode="$(stat -c '%a' "$CONFIG_FILE" 2>/dev/null || true)"
    case "$mode" in
        600 | 400 | '') ;;
        *) warn "$CONFIG_FILE is mode $mode; run: chmod 600 $CONFIG_FILE" ;;
    esac

    if git -C "$SCRIPT_DIR" ls-files --error-unmatch "$CONFIG_FILE" >/dev/null 2>&1; then
        die "$CONFIG_FILE is tracked by git; remove it from the index and keep it private"
    fi

    # .publish_engine is a private shell fragment of simple assignments.
    local config_path="$CONFIG_FILE"
    case "$config_path" in
        /*) ;;
        *) config_path="./$config_path" ;;
    esac
    # shellcheck disable=SC1090
    set +u
    . "$config_path"
    set -u
}

apply_values() {
    SERVER_IMAGE="${SERVER_IMAGE:-}"
    SERVER_REGISTRY_USERNAME="${SERVER_REGISTRY_USERNAME:-}"
    SERVER_REGISTRY_TOKEN="${SERVER_REGISTRY_TOKEN:-}"
    SERVER_PLATFORM="${SERVER_PLATFORM:-linux/amd64}"
    SERVER_IMAGE_TAG="${SERVER_IMAGE_TAG:-}"

    if [ -n "$OPT_IMAGE" ]; then
        SERVER_IMAGE="$OPT_IMAGE"
    fi
    if [ -n "$OPT_PLATFORM" ]; then
        SERVER_PLATFORM="$OPT_PLATFORM"
    fi
    if [ -n "$OPT_TAG" ]; then
        SERVER_IMAGE_TAG="$OPT_TAG"
    fi

    [ -n "$SERVER_IMAGE" ] ||
        die "SERVER_IMAGE is not set; add it to .publish_engine or use --image"
    [ -n "$SERVER_PLATFORM" ] || die "SERVER_PLATFORM must not be empty"
}

require_clean_tree() {
    git -C "$SCRIPT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
        die "not a git working tree: $SCRIPT_DIR"

    local status
    status="$(git -C "$SCRIPT_DIR" status --porcelain)"
    if [ -n "$status" ]; then
        printf '%s\n' "$status" >&2
        die "working tree is dirty; commit or stash changes before publishing"
    fi

    ENGINE_SHA="$(git -C "$SCRIPT_DIR" rev-parse --short HEAD)"
}

derive_tag() {
    if [ -z "$SERVER_IMAGE_TAG" ]; then
        SERVER_IMAGE_TAG="$(date -u +%Y%m%dT%H%M%SZ)-${ENGINE_SHA}"
    fi

    case "$SERVER_IMAGE_TAG" in
        latest) die "SERVER_IMAGE_TAG=latest is reserved for the moving tag; use an immutable tag" ;;
        *[!A-Za-z0-9_.-]*) die "SERVER_IMAGE_TAG contains unsupported characters: $SERVER_IMAGE_TAG" ;;
    esac
    [ -n "$SERVER_IMAGE_TAG" ] || die "SERVER_IMAGE_TAG must not be empty"
    TAG="$SERVER_IMAGE_TAG"
}

validate_image() {
    case "$SERVER_IMAGE" in
        *://*) die "SERVER_IMAGE must be a registry/repository path without a scheme, e.g. ghcr.io/org/ravenest-server" ;;
        *[!a-z0-9._:/-]*) die "SERVER_IMAGE must be lowercase and contain only repository characters: $SERVER_IMAGE" ;;
        /* | */ | *//*) die "SERVER_IMAGE must not start/end with '/' or contain '//': $SERVER_IMAGE" ;;
    esac
    case "${SERVER_IMAGE#*/}" in
        *:*) die "SERVER_IMAGE must not include a tag; put it in SERVER_IMAGE_TAG: $SERVER_IMAGE" ;;
    esac

    case "$SERVER_IMAGE" in
        */*) ;;
        *) die "SERVER_IMAGE must be a full repository path, e.g. ghcr.io/org/ravenest-server" ;;
    esac

    local first="${SERVER_IMAGE%%/*}"
    local rest="${SERVER_IMAGE#*/}"
    case "$first" in
        localhost | *.* | *:*) REGISTRY="$first" ;;
        *)
            if [ "${SERVER_IMAGE#*/}" != "${SERVER_IMAGE}" ] && [ "${rest#*/}" = "$rest" ]; then
                REGISTRY="docker.io"
            else
                die "SERVER_IMAGE must include a registry host, e.g. ghcr.io/org/ravenest-server"
            fi
            ;;
    esac

    case "$REGISTRY" in
        "" | */*) die "could not derive a registry host from SERVER_IMAGE: $SERVER_IMAGE" ;;
    esac
}

validate_credentials() {
    if [ -n "$SERVER_REGISTRY_USERNAME" ] || [ -n "$SERVER_REGISTRY_TOKEN" ]; then
        [ -n "$SERVER_REGISTRY_USERNAME" ] ||
            die "SERVER_REGISTRY_TOKEN is set but SERVER_REGISTRY_USERNAME is empty"
        [ -n "$SERVER_REGISTRY_TOKEN" ] ||
            die "SERVER_REGISTRY_USERNAME is set but SERVER_REGISTRY_TOKEN is empty"
    fi
}

print_result() {
    printf 'SERVER_IMAGE=%s\n' "$SERVER_IMAGE"
    printf 'SERVER_IMAGE_TAG=%s\n' "$TAG"
    if [ "$PUSH_LATEST" = "1" ]; then
        printf 'SERVER_IMAGE_LATEST=latest\n'
    fi
    if [ -n "${DIGEST:-}" ]; then
        printf 'SERVER_IMAGE_DIGEST=%s\n' "$DIGEST"
    fi
}

publish() {
    log "engine: $SCRIPT_DIR @ ${ENGINE_SHA} (clean)"
    log "image: ${SERVER_IMAGE}:${TAG}"
    if [ "$PUSH_LATEST" = "1" ]; then
        log "also publishing: ${SERVER_IMAGE}:latest"
    fi

    if [ "$DRY_RUN" = "1" ]; then
        log "would build ${SERVER_IMAGE}:${TAG} (platform $SERVER_PLATFORM)"
        if [ -n "$SERVER_REGISTRY_USERNAME" ]; then
            log "would log in to $REGISTRY as $SERVER_REGISTRY_USERNAME"
        else
            log "would use the existing Docker credential for $REGISTRY"
        fi
        log "would push ${SERVER_IMAGE}:${TAG}"
        if [ "$PUSH_LATEST" = "1" ]; then
            log "would tag and push ${SERVER_IMAGE}:latest"
        fi
        print_result
        return
    fi

    command -v docker >/dev/null 2>&1 || die "docker not found"
    [ -f server/Dockerfile ] || die "server/Dockerfile is missing"
    [ -f tools/build_server_image.sh ] || die "tools/build_server_image.sh is missing"

    if [ -n "$SERVER_REGISTRY_USERNAME" ]; then
        log "logging in to $REGISTRY as $SERVER_REGISTRY_USERNAME"
        if ! printf '%s' "$SERVER_REGISTRY_TOKEN" |
            docker login "$REGISTRY" --username "$SERVER_REGISTRY_USERNAME" \
                --password-stdin >/dev/null; then
            die "docker login failed for $REGISTRY"
        fi
    else
        warn "no publishing credentials configured; using the existing Docker login"
    fi

    log "building ${SERVER_IMAGE}:${TAG}"
    "$SCRIPT_DIR/tools/build_server_image.sh" "${SERVER_IMAGE}:${TAG}" \
        --platform "$SERVER_PLATFORM"

    log "pushing ${SERVER_IMAGE}:${TAG}"
    docker push "${SERVER_IMAGE}:${TAG}"

    if [ "$PUSH_LATEST" = "1" ]; then
        log "tagging and pushing ${SERVER_IMAGE}:latest"
        docker tag "${SERVER_IMAGE}:${TAG}" "${SERVER_IMAGE}:latest"
        docker push "${SERVER_IMAGE}:latest"
    fi

    DIGEST="$(docker inspect --format '{{index .RepoDigests 0}}' "${SERVER_IMAGE}:${TAG}" 2>/dev/null || true)"
    if [ -n "$DIGEST" ]; then
        log "digest: $DIGEST"
    fi

    log "published ${SERVER_IMAGE}:${TAG}"
    [ "$PUSH_LATEST" = "1" ] && log "published ${SERVER_IMAGE}:latest"
    print_result
}

parse_args "$@"
load_publish_config
apply_values
require_clean_tree
derive_tag
validate_image
validate_credentials
publish
