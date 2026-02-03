#!/bin/bash

# Build script for Web2D Game Backend

set -e

echo "Building Web2D Game Backend..."

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

cd build

# Run CMake
echo "Running CMake..."
cmake ..

# Build
echo "Building..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

echo ""
echo "Build complete!"
echo "Executable: build/game_server"
echo ""
echo "To run the server:"
echo "  cd build"
echo "  ./game_server [port]"
echo ""
