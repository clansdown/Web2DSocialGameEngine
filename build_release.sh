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

if [ -f .docker_repo ]; then
    DOCKER_REPO=$(cat .docker_repo)
else
    echo "Error: .docker_repo file not found"
    echo "Create a .docker_repo file with your Docker registry URL"
    exit 1
fi

if [ -f .docker_creds ]; then
    DOCKER_CREDS=$(cat .docker_creds)
    DOCKER_USER=$(echo "$DOCKER_CREDS" | cut -d':' -f1)
    DOCKER_TOKEN=$(echo "$DOCKER_CREDS" | cut -d':' -f2-)
else
    echo "Error: .docker_creds file not found"
    echo "Create a .docker_creds file with format username:token"
    exit 1
fi

TAG="$1"
if [ -z "$TAG" ]; then
    TAG=$(date +%Y%m%dT%H%M%S)
fi

echo "Building $GAME_NAME release..."
echo "Tag: $TAG"
echo "Repository: $DOCKER_REPO"

mkdir -p server/bin

cd server/build

if [ ! -f CMakeCache.txt ]; then
    echo "Running cmake..."
    cmake ..
fi

echo "Building server..."
make -j$(nproc)

if [ ! -f server ]; then
    echo "Error: Server binary not found after build"
    exit 1
fi

cd "$SCRIPT_DIR"

echo "Building Docker image..."
docker build -t "${GAME_NAME}-server:latest" server/

echo "Tagging image as ${DOCKER_REPO}:$TAG..."
docker tag "${GAME_NAME}-server:latest" "${DOCKER_REPO}:$TAG"

echo "Tagging image as ${DOCKER_REPO}:latest..."
docker tag "${GAME_NAME}-server:latest" "${DOCKER_REPO}:latest"

echo "Logging in to Docker registry..."
echo "$DOCKER_TOKEN" | docker login "$DOCKER_REPO" -u "$DOCKER_USER" --password-stdin

if [ $? -ne 0 ]; then
    echo "Error: Docker login failed"
    exit 1
fi

echo "Pushing image to ${DOCKER_REPO}:$TAG..."
docker push "${DOCKER_REPO}:$TAG"

echo "Pushing image to ${DOCKER_REPO}:latest..."
docker push "${DOCKER_REPO}:latest"

echo ""
echo "================================"
echo "Release complete!"
echo "================================"
echo "Game: $GAME_NAME"
echo "Tag: $TAG"
echo "Repository: $DOCKER_REPO"
echo ""
echo "To deploy this image:"
echo "  docker pull ${DOCKER_REPO}:$TAG"
echo "================================"