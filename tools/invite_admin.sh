#!/usr/bin/env bash
#
# Host-side wrapper for the invite_admin CLI inside the running Ravenest image.
#
# Usage:
#   ./tools/invite_admin.sh create --count N
#   ./tools/invite_admin.sh list
#   ./tools/invite_admin.sh revoke <invitation-id>

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/_admin_common.sh
. "$SCRIPT_DIR/_admin_common.sh"

admin_load_compose_env
admin_exec_in_app /app/invite_admin "$@"
