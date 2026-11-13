#!/usr/bin/env bash
#
# push_text.sh — mirror game/text/ to the API server and reload.
#
# Skips the client build, R2, and Pages. The server content scope is `text`;
# the reload callback clears the TextManager cache so new text is served
# without an API restart. Pass --no-reload to push files only.

set -euo pipefail

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$TOOLS_DIR/install.sh" \
    --skip-build --skip-r2 --skip-pages \
    --server-content text "$@"
