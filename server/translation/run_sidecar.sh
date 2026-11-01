#!/usr/bin/env bash
# Runs the CPU MADLAD translation sidecar (localhost only).
set -euo pipefail
cd "$(dirname "$0")"
exec python3 sidecar.py --host 127.0.0.1 --port "${MADLAD_PORT:-8790}" \
  --model "${MADLAD_MODEL_DIR:-models/madlad400-3b-ct2-int8}" "$@"
