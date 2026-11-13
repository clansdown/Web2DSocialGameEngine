#!/bin/bash
# Build the server image and push an immutable tag to the configured registry.
# Legacy: prefer `./publish_engine_server_image.sh`, which reads .publish_engine,
# refuses dirty trees, and publishes an immutable tag plus latest. This script
# remains for the .docker_repo/.docker_creds workflow. For deployment use
# tools/install.sh, which pulls an already-published image.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ -f .game_name ]; then
    GAME_NAME=$(cat .game_name | tr '[:upper:]' '[:lower:]')
else
    echo "Error: .game_name file not found"
    echo "Create a .game_name file in the root directory with your game name (e.g., Ravenest)"
    exit 1
fi

if [ -f .docker_repo ]; then
    DOCKER_REPO=$(tr -d '[:space:]' < .docker_repo)
else
    echo "Error: .docker_repo file not found"
    echo "Create a .docker_repo file with the full repository path (e.g. ghcr.io/org/ravenest-server)"
    exit 1
fi

if [ -f .docker_creds ]; then
    DOCKER_CREDS=$(tr -d '\r\n' < .docker_creds)
    DOCKER_USER=$(echo "$DOCKER_CREDS" | cut -d':' -f1)
    DOCKER_TOKEN=$(echo "$DOCKER_CREDS" | cut -d':' -f2-)
else
    echo "Error: .docker_creds file not found"
    echo "Create a .docker_creds file with format username:token"
    exit 1
fi

TAG="$1"
if [ -z "$TAG" ]; then
    TAG=$(date -u +%Y%m%dT%H%M%SZ)-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
fi

REGISTRY="${DOCKER_REPO%%/*}"
if [ "$REGISTRY" = "$DOCKER_REPO" ]; then
    echo "Error: .docker_repo must be a full repository path (registry/org/name)"
    exit 1
fi

IMAGE="${DOCKER_REPO}:${TAG}"
echo "Building $GAME_NAME release..."
echo "Tag: $TAG"
echo "Repository: $DOCKER_REPO"

"$SCRIPT_DIR/tools/build_server_image.sh" "$IMAGE"

echo "Logging in to $REGISTRY..."
echo "$DOCKER_TOKEN" | docker login "$REGISTRY" -u "$DOCKER_USER" --password-stdin

echo "Pushing $IMAGE..."
docker push "$IMAGE"

echo ""
echo "================================"
echo "Release complete!"
echo "================================"
echo "Game: $GAME_NAME"
echo "Tag: $TAG"
echo "Repository: $DOCKER_REPO"
echo ""
echo "To deploy this image:"
echo "  docker pull $IMAGE"
echo "================================"
