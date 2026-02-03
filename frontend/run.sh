#!/bin/bash

# Run script for Web2D Game Frontend

set -e

PORT=${1:-8000}

echo "Starting Web2D Game Frontend on port $PORT..."
echo "Open your browser to: http://localhost:$PORT"
echo ""
echo "Note: Make sure the backend server is running on port 8080"
echo "Press Ctrl+C to stop the server"
echo ""

# Try to use Python 3 HTTP server
if command -v python3 &> /dev/null; then
    python3 -m http.server $PORT
elif command -v python &> /dev/null; then
    python -m SimpleHTTPServer $PORT
else
    echo "Error: Python not found. Please install Python or use another HTTP server."
    exit 1
fi
