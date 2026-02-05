#!/bin/bash
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

echo "Building $GAME_NAME server..."

if [ ! -f server/CMakeLists.txt ]; then
    echo "Error: server/CMakeLists.txt not found"
    exit 1
fi

cd server/build

if [ ! -f CMakeCache.txt ]; then
    echo "Running cmake..."
    cmake ..
else
    echo "CMake cache exists, skipping cmake configuration"
fi

echo "Building server..."
make -j$(nproc)

if [ ! -f server ]; then
    echo "Error: Server binary not found after build"
    exit 1
fi

echo "Building Docker image..."
cd "$SCRIPT_DIR"
docker build -t "${GAME_NAME}-server:latest" server/

DOCKER_TAR="server/bin/${GAME_NAME}-server.tar"
echo "Saving Docker image to $DOCKER_TAR..."
docker save "${GAME_NAME}-server:latest" -o "$DOCKER_TAR"

IMAGE_SIZE=$(docker images "${GAME_NAME}-server:latest" --format "{{.Size}}" | head -1)
echo ""
echo "================================"
echo "Build complete!"
echo "================================"
echo "Docker image: ${GAME_NAME}-server:latest"
echo "Image size: $IMAGE_SIZE"
echo "Saved to: $DOCKER_TAR"
echo ""
echo "To load this image on another machine:"
echo "  docker load < $DOCKER_TAR"
echo "================================"