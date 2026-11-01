#!/usr/bin/env bash
# Guards the PostgreSQL table-doc sweep: no legacy SQLite database names,
# SQLite autoincrement syntax, or SQLite-specific prose in server/tables/*.md.
#
# Usage: bash tools/check_table_docs.sh
# Exit: 0 clean, 1 findings
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TABLES_DIR="$ROOT/server/tables"

if [ ! -d "$TABLES_DIR" ]; then
    echo "check_table_docs: missing $TABLES_DIR" >&2
    exit 1
fi

PATTERN='game\.db|chat\.db|staff\.db|invites\.db|AUTOINCREMENT|SQLite'
status=0
while IFS= read -r match; do
    echo "$match"
    status=1
done < <(grep -RInE "$PATTERN" "$TABLES_DIR" --include='*.md' || true)

if [ "$status" -ne 0 ]; then
    echo "check_table_docs: legacy SQLite names/types found in server/tables" >&2
    exit 1
fi

echo "check_table_docs: clean"
exit 0
