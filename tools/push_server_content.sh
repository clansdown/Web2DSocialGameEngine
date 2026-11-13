#!/usr/bin/env bash
#
# push_server_content.sh — mirror all game/ content and reload.
#
# Skips the client build, R2, and Pages, and does not pull the image or rewrite
# env/compose/tools. Use this when config, images, and text changed together
# and the server runtime is unchanged. Pass --no-reload to push files only.

set -euo pipefail

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$TOOLS_DIR/install.sh" \
    --skip-build --skip-r2 --skip-pages \
    --server-content all "$@"
