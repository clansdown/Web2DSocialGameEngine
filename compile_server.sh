#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/server"

QUIET=false
VERBOSE=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -q, --quiet     Suppress build output"
    echo "  -v, --verbose   Show verbose build output"
    echo "  -h, --help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0              # Compile with normal output"
    echo "  $0 -q           # Compile silently"
    echo "  $0 -v           # Compile with verbose output"
    echo ""
    echo "Exit codes:"
    echo "  0               Compilation successful"
    echo "  1               Build failure"
}

while getopts "qvh-:" opt; do
    case $opt in
        q)
            QUIET=true
            ;;
        v)
            VERBOSE=true
            ;;
        h)
            print_usage
            exit 0
            ;;
        -)
            case "${OPTARG}" in
                quiet)
                    QUIET=true
                    ;;
                verbose)
                    VERBOSE=true
                    ;;
                help)
                    print_usage
                    exit 0
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
    esac
done

mkdir -p build

if [ ! -f build/CMakeCache.txt ]; then
    if [ "$QUIET" = false ]; then
        echo "Running cmake..."
    fi
    if [ "$QUIET" = true ]; then
        cd build
        cmake .. >/dev/null 2>&1
    elif [ "$VERBOSE" = true ]; then
        cd build
        cmake .. -v
    else
        cd build
        cmake ..
    fi
    
    if [ $? -ne 0 ]; then
        echo "Error: CMake configuration failed"
        exit 1
    fi
else
    cd build
fi

if [ "$QUIET" = false ]; then
    echo "Building server..."
fi

if [ "$QUIET" = true ]; then
    if ! make -j$(nproc) >/dev/null 2>&1; then
        exit 1
    fi
elif [ "$VERBOSE" = true ]; then
    if ! make -j$(nproc) VERBOSE=1; then
        exit 1
    fi
else
    if ! make -j$(nproc); then
        exit 1
    fi
fi

if [ "$QUIET" = false ]; then
    echo "Compilation successful"
fi

exit 0
