#!/bin/bash
# Build the server Docker image and save it as a tarball for offline transfer.
# The image is built by server/Dockerfile, so no host-side CMake/make is needed.
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

mkdir -p server/bin

IMAGE="${GAME_NAME}-server:latest"
echo "Building $GAME_NAME server image..."
"$SCRIPT_DIR/tools/build_server_image.sh" "$IMAGE"

DOCKER_TAR="server/bin/${GAME_NAME}-server.tar"
echo "Saving Docker image to $DOCKER_TAR..."
docker save "$IMAGE" -o "$DOCKER_TAR"

IMAGE_SIZE=$(docker images "$IMAGE" --format "{{.Size}}" | head -1)
echo ""
echo "================================"
echo "Build complete!"
echo "================================"
echo "Docker image: $IMAGE"
echo "Image size: $IMAGE_SIZE"
echo "Saved to: $DOCKER_TAR"
echo ""
echo "To load this image on another machine:"
echo "  docker load < $DOCKER_TAR"
echo "================================"
