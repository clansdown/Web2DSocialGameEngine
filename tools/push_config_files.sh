#!/usr/bin/env bash
#
# push_config_files.sh — mirror game/config/ to the API server and reload.
#
# Skips the client build, R2, and Pages. The server content scope is `config`,
# so only game/config/ is mirrored; the installed reload helper (SIGHUP) is
# invoked afterwards. Pass --no-reload to push files without reloading.

set -euo pipefail

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$TOOLS_DIR/install.sh" \
    --skip-build --skip-r2 --skip-pages \
    --server-content config "$@"
