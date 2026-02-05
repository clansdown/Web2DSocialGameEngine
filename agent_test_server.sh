#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

AGENT_DB_DIR="$SCRIPT_DIR/.agent_test_db"
PORT=3290
KEEP_DB=false
LOG_FILE=""
QUIET=false
VERBOSE=false
TEST_NUM_REQUESTS=0
TEST_TIMEOUT=0

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -N N         Exit after N requests processed (required)"
    echo "  -M M         Exit after M seconds (required)"
    echo "  -p PORT      Port to bind (default: 3290)"
    echo "  -l FILE      Write logs to file"
    echo "  -q           Quiet mode (minimal output)"
    echo "  -v           Verbose mode"
    echo "  --keep-db    Keep databases after exit (default: wipe)"
    echo "  -h           Show this help message"
    echo ""
    echo "Note: At least one of -N or -M must be specified"
    echo ""
    echo "Examples:"
    echo "  $0 -N 100              # Process 100 requests then exit"
    echo "  $0 -M 30               # Run for 30 seconds then exit"
    echo "  $0 -N 50 -M 60         # Exit on whichever limit reached first"
    echo "  $0 -N 10 -p 8080       # Custom port, process 10 requests"
}

while getopts "N:M:p:l:qv-:" opt; do
    case $opt in
        N)
            TEST_NUM_REQUESTS="$OPTARG"
            ;;
        M)
            TEST_TIMEOUT="$OPTARG"
            ;;
        p)
            PORT="$OPTARG"
            ;;
        l)
            LOG_FILE="$OPTARG"
            ;;
        q)
            QUIET=true
            ;;
        v)
            VERBOSE=true
            ;;
        -)
            case "${OPTARG}" in
                keep-db)
                    KEEP_DB=true
                    ;;
                *)
                    echo "Unknown option: --${OPTARG}"
                    print_usage
                    exit 1
                    ;;
            esac
            ;;
        \?)
            print_usage
            exit 1
            ;;
        :)
            echo "Option -$OPTARG requires an argument"
            print_usage
            exit 1
            ;;
    esac
done

if [ "$TEST_NUM_REQUESTS" -eq 0 ] && [ "$TEST_TIMEOUT" -eq 0 ]; then
    echo "Error: At least one of -N or -M must be specified"
    print_usage
    exit 1
fi

if [ -f .game_name ]; then
    GAME_NAME=$(cat .game_name | tr '[:upper:]' '[:lower:]')
else
    echo "Warning: .game_name file not found, using default name"
    GAME_NAME="server"
fi

mkdir -p "$AGENT_DB_DIR"
rm -rf "$AGENT_DB_DIR"/*

if [ "$QUIET" = true ]; then
    VERBOSE_FLAG="--quiet"
elif [ "$VERBOSE" = true ]; then
    VERBOSE_FLAG="--verbose"
else
    VERBOSE_FLAG=""
fi

if [ -n "$VERBOSE_FLAG" ]; then
    ECHO_ARGS="$VERBOSE_FLAG"
else
    ECHO_ARGS=""
fi

cd server/build

if [ ! -f CMakeCache.txt ]; then
    if [ "$QUIET" = false ]; then
        echo "Running cmake..."
    fi
    cmake .. >/dev/null 2>&1
fi

if [ "$QUIET" = false ]; then
    echo "Building server..."
fi
make -j$(nproc) >/dev/null 2>&1

if [ ! -f server ]; then
    echo "Error: Server binary not found after build"
    exit 1
fi

cd "$SCRIPT_DIR"

if [ "$QUIET" = false ]; then
    echo "Starting $GAME_NAME test server on port $PORT..."
    echo "  Requests limit: ${TEST_NUM_REQUESTS:-unlimited}"
    echo "  Time limit: ${TEST_TIMEOUT:-unlimited} seconds"
    echo "  Database dir: $AGENT_DB_DIR"
fi

START_TIME=$(date +%s)

if [ -n "$LOG_FILE" ]; then
    ./server/build/server --db-dir "$AGENT_DB_DIR" --port "$PORT" $VERBOSE_FLAG \
        --test-num-requests "$TEST_NUM_REQUESTS" --test-timeout-seconds "$TEST_TIMEOUT" \
        > "$LOG_FILE" 2>&1 &
else
    ./server/build/server --db-dir "$AGENT_DB_DIR" --port "$PORT" $VERBOSE_FLAG \
        --test-num-requests "$TEST_NUM_REQUESTS" --test-timeout-seconds "$TEST_TIMEOUT" \
        >/dev/null 2>&1 &
fi

SERVER_PID=$!

sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Error: Server failed to start"
    exit 2
fi

if [ "$QUIET" = false ]; then
    echo "Server started (PID: $SERVER_PID)"
    echo "Waiting for completion..."
fi

wait "$SERVER_PID"
EXIT_CODE=$?

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

if [ "$QUIET" = false ]; then
    echo ""
    echo "================================"
    echo "Test run complete"
    echo "================================"
    echo "Exit code: $EXIT_CODE"
    echo "Time elapsed: ${ELAPSED}s"
    echo "================================"
fi

if [ "$KEEP_DB" = false ]; then
    if [ "$QUIET" = false ]; then
        echo "Cleaning up test database directory..."
    fi
    rm -rf "$AGENT_DB_DIR"
fi

exit $EXIT_CODE