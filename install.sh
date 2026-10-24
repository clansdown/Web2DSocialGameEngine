#!/usr/bin/env bash
#
# install.sh — build and publish the client, public game assets, and server content.
#
# The engine is generic: site-specific values (site repo, R2 bucket, API server)
# come from a gitignored .deploy file. See docs/publishing.md for setup, the
# .deploy reference, cache policy, and verification.
#
# Usage:
#   ./install.sh [--dry-run] [--skip-build] [--skip-r2] [--skip-pages] [--skip-server]
#
# Publish order is build -> R2 -> Pages -> API server. R2 goes before Pages so the
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

Builds the client and publishes to every destination configured in .deploy.
With no destinations configured, the script fails before publishing.

Stages (run in this order):
  build   npm ci && npm run build -> client/dist
  r2      game/images, game/sounds, game/music -> R2 bucket
  pages   client/dist + overlay -> private Pages site repo, then commit and push
  server  game/ -> API server content mirror (rsync --delete)

Options:
  --dry-run       Print publish actions without writing to R2, the site repo,
                  or the API server. The local client build still runs.
  --skip-build    Reuse an existing client/dist (it must contain index.html).
  --skip-r2       Skip the R2 upload stage.
  --skip-pages    Skip the Pages site-repo stage.
  --skip-server   Skip the API server mirror stage.
  -h, --help      Show this help.

Environment:
  DRY_RUN=1       Same as --dry-run.

Destinations are enabled by setting their keys in .deploy: SITE_REPO (Pages),
R2_BUCKET (R2), and SERVER_HOST (API server). Each enabled stage validates its
own keys. R2 credentials are exported only for the aws commands and are never
printed.
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
    CF_API_TOKEN="${CF_API_TOKEN:-}"
    CF_ZONE_ID="${CF_ZONE_ID:-}"
}

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

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------

preflight() {
    [ -d client ] || die "client/ not found; run install.sh from the engine repository"

    local private_path
    for private_path in .deploy backup.sh push_to_production.sh; do
        if git -C "$SCRIPT_DIR" ls-files --error-unmatch "$private_path" >/dev/null 2>&1; then
            die "$private_path is tracked by git; remove it from the index and keep it private"
        fi
    done

    preflight_git
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
        log "build: skipped"
        return
    fi

    command -v npm >/dev/null 2>&1 || die "npm not found"
    [ -d client/SimpleGame/ui ] ||
        die "client/SimpleGame/ui is missing; clone SimpleGame into client/SimpleGame (see README.md)"
    [ -f client/package-lock.json ] || die "client/package-lock.json is missing; npm ci requires it"

    log "build: npm ci"
    npm --prefix client ci --no-audit --no-fund

    log "build: npm run build"
    npm --prefix client run build

    [ -f client/dist/index.html ] || die "build did not produce client/dist/index.html"
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
# Stage 4 — API server content mirror
# ---------------------------------------------------------------------------

stage_server() {
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
    stage_server
else
    log "server: skipped (not configured or --skip-server)"
fi

log "done"
