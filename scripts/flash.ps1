#!/usr/bin/env pwsh
# Build, flash and monitor the firmware. Requires ESP-IDF to be exported
# (run the "ESP-IDF PowerShell" or export.ps1 first). Usage: scripts/flash.ps1 [-Port COMx]
param([string]$Port)
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    if ($Port) {
        idf.py -p $Port flash monitor
    }
    else {
        idf.py flash monitor
    }
}
finally {
    Pop-Location
}
