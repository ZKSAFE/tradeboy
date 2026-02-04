#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v sdl2-config >/dev/null 2>&1; then
  echo "ERROR: sdl2-config not found. Install SDL2 first: brew install sdl2" >&2
  exit 1
fi

make -C "$SCRIPT_DIR" tradeboy-macos

echo "Built: $SCRIPT_DIR/output/tradeboy-macos"
