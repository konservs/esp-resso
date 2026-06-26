#!/usr/bin/env bash
# Apply clang-format in place to all tracked C sources/headers.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

# git ls-files naturally excludes build/ and fetched dependencies.
mapfile -t files < <(git ls-files '*.c' '*.h')
if [ ${#files[@]} -gt 0 ]; then
    clang-format -i "${files[@]}"
    echo "formatted ${#files[@]} files"
fi
