#!/usr/bin/env bash
#
# push_assets.sh — upload public assets to R2 and mirror game/images/.
#
# Runs the R2 stage when R2_BUCKET is configured (game/images, game/sounds,
# game/music) and mirrors game/images/ to the server content tree. The server
# does not serve /sounds or /music; those are delivered from the assets origin.
# No reload is needed (the image cache rescans on directory mtime).
#
# Pass --skip-server for an R2-only push.

set -euo pipefail

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$TOOLS_DIR/install.sh" \
    --skip-build --skip-pages \
    --server-content assets "$@"
