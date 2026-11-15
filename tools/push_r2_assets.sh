#!/usr/bin/env bash
#
# push_r2_assets.sh — upload public assets to R2 only.
#
# Takes no arguments by design; extra arguments are rejected. Uploads
# game/images, game/sounds, and game/music. It does not mirror game/images to
# the API server and does not restart it; use push_assets.sh for both.
#
# Requires R2_BUCKET, R2_ENDPOINT, R2_ACCESS_KEY_ID, and R2_SECRET_ACCESS_KEY in
# .deploy. Set DRY_RUN=1 for a preview.

set -euo pipefail

if [ "$#" -ne 0 ]; then
    printf 'error: tools/push_r2_assets.sh takes no arguments\n' >&2
    exit 2
fi

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$TOOLS_DIR/install.sh" \
    --skip-build --skip-pages --skip-server --require-r2
