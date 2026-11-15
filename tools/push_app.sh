#!/usr/bin/env bash
#
# push_app.sh — build the Svelte client and publish only the Pages shell.
#
# Takes no arguments by design; extra arguments are rejected. Always rebuilds
# (no --skip-build), never uploads R2 assets, and never touches the API server.
#
# Requires SITE_REPO (plus PUBLIC_API_ORIGIN and PUBLIC_ASSETS_ORIGIN) in
# .deploy. Set DRY_RUN=1 for a preview.

set -euo pipefail

if [ "$#" -ne 0 ]; then
    printf 'error: tools/push_app.sh takes no arguments\n' >&2
    exit 2
fi

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$TOOLS_DIR/install.sh" --skip-r2 --skip-server --require-pages
