#!/usr/bin/env bash
# Build, flash and monitor the firmware. Requires ESP-IDF to be exported
# (run `. $IDF_PATH/export.sh` first). Usage: scripts/flash.sh [PORT]
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

if [ -n "${1:-}" ]; then
    idf.py -p "$1" flash monitor
else
    idf.py flash monitor
fi
