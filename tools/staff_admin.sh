#!/usr/bin/env bash
#
# Host-side wrapper for the staff_admin CLI inside the running Ravenest image.
#
# Usage:
#   ./tools/staff_admin.sh grant <username|account_id> superadmin          # superuser
#   ./tools/staff_admin.sh grant <username|account_id> customer_service    # standard admin
#   ./tools/staff_admin.sh grant <username|account_id> support_agent       # support only
#   ./tools/staff_admin.sh revoke <username|account_id>
#   ./tools/staff_admin.sh list
#   ./tools/staff_admin.sh roles
#   ./tools/staff_admin.sh role <role_id|alias>
#   ./tools/staff_admin.sh roles --verbose
#
# Aliases: superuser=superadmin, staff=customer_service,
#          support=support_agent, avatar=avatar_moderator

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/_admin_common.sh
. "$SCRIPT_DIR/_admin_common.sh"

admin_load_compose_env
admin_exec_in_app /app/staff_admin "$@"
