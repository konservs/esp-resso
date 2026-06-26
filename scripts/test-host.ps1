#!/usr/bin/env pwsh
# Configure, build and run the host unit tests (Windows).
# Requires: cmake >= 3.21, ninja, MinGW-w64 gcc, git (for FetchContent).
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
Push-Location (Join-Path $repo "tests")
try {
    cmake --preset host
    cmake --build --preset host
    ctest --preset host --output-on-failure
}
finally {
    Pop-Location
}
