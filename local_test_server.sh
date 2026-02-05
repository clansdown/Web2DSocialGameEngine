#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PID_FILE="$SCRIPT_DIR/.local_test_server.pid"
PORT=2290
DEBUG=false
FOREGROUND=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -p PORT     Port to bind (default: 2290)"
    echo "  -d         Debug mode (detailed request/response logs)"
    echo "  -f         Run in foreground"
    echo "  -h         Show this help message"
}

while getopts "p:dfh" opt; do
    case $opt in
        p)
            PORT="$OPTARG"
            ;;
        d)
            DEBUG=true
            ;;
        f)
            FOREGROUND=true
            ;;
        h)
            print_usage
            exit 0
            ;;
        \?)
            echo "Invalid option: -$OPTARG" >&2
            print_usage
            exit 1
            ;;
    esac
done

if [ -f .game_name ]; then
    GAME_NAME=$(cat .game_name | tr '[:upper:]' '[:lower:]')
else
    echo "Warning: .game_name file not found, using default name"
    GAME_NAME="server"
fi

echo "Building server if needed..."
cd server/build

if [ ! -f CMakeCache.txt ]; then
    echo "Running cmake..."
    cmake ..
fi

make -j$(nproc)

if [ ! -f server ]; then
    echo "Error: Server binary not found after build"
    exit 1
fi

cd "$SCRIPT_DIR"

if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo "Stopping existing server (PID: $OLD_PID)..."
        kill "$OLD_PID" 2>/dev/null || true
        sleep 2
    fi
    rm -f "$PID_FILE"
fi

if [ "$DEBUG" = true ]; then
    VERBOSE_FLAG="--verbose"
else
    VERBOSE_FLAG="--quiet"
fi

cd "$SCRIPT_DIR"

if [ "$FOREGROUND" = true ]; then
    echo "Starting $GAME_NAME server in foreground on port $PORT..."
    ./server/build/server --port "$PORT" $VERBOSE_FLAG
else
    echo "Starting $GAME_NAME server in background on port $PORT..."
    ./server/build/server --port "$PORT" $VERBOSE_FLAG >/dev/null 2>&1 &

    SERVER_PID=$!
    echo "$SERVER_PID" > "$PID_FILE"

    sleep 2

    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "Error: Server failed to start"
        rm -f "$PID_FILE"
        exit 1
    fi

    echo "Server started successfully (PID: $SERVER_PID)"
    echo ""
    echo "================================"
    echo "Server running at:"
    echo "  http://localhost:$PORT"
    echo ""
    echo "To stop server:"
    echo "  kill $SERVER_PID"
    echo "  or: kill \$(cat $PID_FILE)"
    echo "================================"
fi