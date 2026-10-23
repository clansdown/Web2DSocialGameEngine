#!/bin/bash
# Runs the server from test_run/ so its databases and uploads never land in
# game/ (which push_to_production.sh rsyncs). `--content` points the server at
# game/ for config/, images/, and text/.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN_DIR="$SCRIPT_DIR/test_run"
mkdir -p "$RUN_DIR"
cd "$RUN_DIR" && exec "$SCRIPT_DIR/server/build/server" --content "$SCRIPT_DIR/game" "$@"
