#!/bin/bash
# Build a debug (and optionally sanitized) server binary.
#
#   ./compile_debug-server.sh                 debug (-g -O0)  -> server/build-debug/
#   ./compile_debug-server.sh --tsan          ThreadSanitizer -> server/build-tsan/
#   ./compile_debug-server.sh --asan          Address+UBSan   -> server/build-asan/
#   ./compile_debug-server.sh --build-dir DIR custom build dir
#
# Debug/sanitizer trees reuse the normal tree's uWebSockets clone + prebuilt
# uSockets.a and sqlite_modern_cpp source (no re-download). Run
# ./compile_server.sh once first to populate server/build/_deps.
#
# Exit codes: 0 = success, 1 = failure.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/server"

QUIET=false
VERBOSE=false
TSAN=false
ASAN=false
BUILD_DIR=""

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -q, --quiet         Suppress build output"
    echo "  -v, --verbose       Show verbose build output"
    echo "      --tsan          ThreadSanitizer build (server/build-tsan)"
    echo "      --asan          AddressSanitizer + UBSan build (server/build-asan)"
    echo "      --build-dir DIR Custom build directory (default depends on options)"
    echo "  -h, --help          Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                  # Debug build"
    echo "  $0 --tsan           # ThreadSanitizer build (used by the combat tests)"
    echo "  $0 --asan -q        # Address+UBSan, quiet"
    echo ""
    echo "Sanitizer binaries are slow; use them for tests, never production."
    echo "Exit codes:"
    echo "  0                   Compilation successful"
    echo "  1                   Build failure"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -q|--quiet) QUIET=true ;;
        -v|--verbose) VERBOSE=true ;;
        --tsan) TSAN=true ;;
        --asan) ASAN=true ;;
        --build-dir) shift; BUILD_DIR="${1:-}" ;;
        --build-dir=*) BUILD_DIR="${1#*=}" ;;
        -h|--help) print_usage; exit 0 ;;
        *) echo "Unknown option: $1"; print_usage; exit 1 ;;
    esac
    shift
done

if [ "$TSAN" = true ] && [ "$ASAN" = true ]; then
    echo "Error: --tsan and --asan are mutually exclusive" >&2
    exit 1
fi

if [ -z "$BUILD_DIR" ]; then
    if [ "$TSAN" = true ]; then
        BUILD_DIR="build-tsan"
    elif [ "$ASAN" = true ]; then
        BUILD_DIR="build-asan"
    else
        BUILD_DIR="build-debug"
    fi
fi

UWS_SHARED="$SCRIPT_DIR/server/build/_deps/uwebsockets-src"
SQLITE_SHARED="$SCRIPT_DIR/server/build/_deps/sqlite_modern_cpp-src"
if [ ! -d "$UWS_SHARED" ]; then
    echo "Error: run ./compile_server.sh once first (needs server/build/_deps)." >&2
    exit 1
fi

CXX_FLAGS="-g -O0 -fno-omit-frame-pointer"
LINK_FLAGS=""
if [ "$TSAN" = true ]; then
    CXX_FLAGS="$CXX_FLAGS -fsanitize=thread"
    LINK_FLAGS="-fsanitize=thread"
fi
if [ "$ASAN" = true ]; then
    CXX_FLAGS="$CXX_FLAGS -fsanitize=address,undefined"
    LINK_FLAGS="-fsanitize=address,undefined"
fi

CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=Debug"
    "-DCMAKE_CXX_FLAGS=$CXX_FLAGS"
    "-DUWS_SRC_DIR=$UWS_SHARED"
)
if [ -n "$LINK_FLAGS" ]; then
    CMAKE_ARGS+=("-DCMAKE_EXE_LINKER_FLAGS=$LINK_FLAGS")
fi
if [ -d "$SQLITE_SHARED" ]; then
    CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_SQLITE_MODERN_CPP=$SQLITE_SHARED")
fi

mkdir -p "$BUILD_DIR"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    if [ "$QUIET" = false ]; then
        echo "Running cmake ($BUILD_DIR)..."
    fi
    if [ "$QUIET" = true ]; then
        ( cd "$BUILD_DIR" && cmake .. "${CMAKE_ARGS[@]}" >/dev/null 2>&1 )
    elif [ "$VERBOSE" = true ]; then
        ( cd "$BUILD_DIR" && cmake .. "${CMAKE_ARGS[@]}" -v )
    else
        ( cd "$BUILD_DIR" && cmake .. "${CMAKE_ARGS[@]}" )
    fi

    if [ $? -ne 0 ]; then
        echo "Error: CMake configuration failed"
        exit 1
    fi
fi

cd "$BUILD_DIR"

if [ "$QUIET" = false ]; then
    echo "Building debug server ($BUILD_DIR)..."
fi

if [ "$QUIET" = true ]; then
    if ! make -j"$(nproc)" >/dev/null 2>&1; then
        exit 1
    fi
elif [ "$VERBOSE" = true ]; then
    if ! make -j"$(nproc)" VERBOSE=1; then
        exit 1
    fi
else
    if ! make -j"$(nproc)"; then
        exit 1
    fi
fi

echo "Debug server built: server/$BUILD_DIR/server"
exit 0
