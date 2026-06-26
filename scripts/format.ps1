#!/usr/bin/env pwsh
# Apply clang-format in place to all tracked C sources/headers.
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    $files = git ls-files '*.c' '*.h'
    if ($files) {
        $files | ForEach-Object { clang-format -i $_ }
        Write-Host "formatted $($files.Count) files"
    }
}
finally {
    Pop-Location
}
