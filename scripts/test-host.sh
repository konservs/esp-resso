#!/usr/bin/env bash
# Configure, build and run the host unit tests (Linux/macOS, or Git Bash).
# Requires: cmake >= 3.21, ninja, a C compiler, git (for FetchContent).
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo/tests"

cmake --preset host
cmake --build --preset host
ctest --preset host --output-on-failure
