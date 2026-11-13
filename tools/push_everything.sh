#!/usr/bin/env bash
#
# push_everything.sh — full client build and deployment.
#
# Runs tools/install.sh with the full server content scope: build -> R2 ->
# Pages -> server (pull image, mirror game/, write env/compose/tools, restart).
# Any arguments are forwarded, e.g. --dry-run, --skip-build, --skip-pages.

set -euo pipefail

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$TOOLS_DIR/install.sh" --server-content full "$@"
