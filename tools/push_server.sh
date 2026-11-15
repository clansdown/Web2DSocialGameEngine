#!/usr/bin/env bash
#
# push_server.sh — full API-server deploy without rebuilding the client or
# publishing R2/Pages.
#
# Takes no arguments by design; extra arguments are rejected. Runs the full
# server stage: pull the published image (Docker mode), mirror all game/
# content, write the runtime env and compose files, install the server tools,
# and restart.
#
# Requires SERVER_HOST and SERVER_PATH in .deploy; the server stage validates
# the remaining runtime settings. Set DRY_RUN=1 for a preview.

set -euo pipefail

if [ "$#" -ne 0 ]; then
    printf 'error: tools/push_server.sh takes no arguments\n' >&2
    exit 2
fi

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$TOOLS_DIR/install.sh" \
    --skip-build --skip-r2 --skip-pages \
    --server-content full --require-server
