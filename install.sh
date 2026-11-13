#!/usr/bin/env bash
#
# install.sh — compatibility launcher for the real deployment script.
#
# The implementation lives at tools/install.sh. This launcher keeps the
# historical `./install.sh` entry point working; new scripts and docs should
# call tools/install.sh directly (or one of the tools/push_*.sh wrappers).

set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exec bash "$ROOT_DIR/tools/install.sh" "$@"
