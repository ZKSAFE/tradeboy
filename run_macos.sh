#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -x "$SCRIPT_DIR/output/tradeboy-macos" ]; then
  echo "ERROR: binary not found. Run ./build_macos.sh first." >&2
  exit 1
fi

cd "$SCRIPT_DIR"
exec "$SCRIPT_DIR/output/tradeboy-macos"
