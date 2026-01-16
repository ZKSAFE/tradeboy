#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

make -C "$SCRIPT_DIR" tradeboy-armhf-docker
