#!/usr/bin/env bash
# Build the production server image from server/ (the Docker build context).
#
# Usage: tools/build_server_image.sh <image:tag> [--platform PLATFORM]
# Env:   SERVER_PLATFORM (default linux/amd64)
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
    echo "usage: $0 <image:tag> [--platform PLATFORM]" >&2
    exit 1
fi
shift || true

PLATFORM="${SERVER_PLATFORM:-linux/amd64}"
if [ "${1:-}" = "--platform" ] && [ -n "${2:-}" ]; then
    PLATFORM="$2"
fi

command -v docker >/dev/null 2>&1 || {
    echo "docker not found" >&2
    exit 1
}

echo "Building $IMAGE (platform $PLATFORM)..."
docker build --platform "$PLATFORM" \
    -f "$ROOT/server/Dockerfile" \
    -t "$IMAGE" \
    "$ROOT/server"
