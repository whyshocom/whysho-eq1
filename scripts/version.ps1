<#
.SYNOPSIS
    Derive the project version for whysho EQ1 packaging (PowerShell equivalent of version.sh).

.DESCRIPTION
    Resolution order:
      1. $env:WHYSHO_VERSION, if set.
      2. The version in CMakeLists.txt (`project(... VERSION X.Y.Z)`): the one
         place the version is written, and what the plugin binary reports.

    A leading `v` is stripped so the output is a bare semantic version
    (e.g. `1.2.3`). The resolved version string is written to stdout.

.EXAMPLE
    $version = & scripts/version.ps1
#>

$ErrorActionPreference = 'Stop'


function Remove-VPrefix {
    param([string]$Value)
    return ($Value -replace '^[vV]', '')
}

# 1. Explicit override via environment.
if ($env:WHYSHO_VERSION) {
    Write-Output (Remove-VPrefix $env:WHYSHO_VERSION)
    exit 0
}

# 2. CMakeLists.txt.
$cmake = Get-Content (Join-Path $PSScriptRoot '..\CMakeLists.txt') -Raw
if ($cmake -match 'project\([^)]*VERSION\s+(\d+\.\d+\.\d+)') {
    Write-Output $Matches[1]
    exit 0
}
Write-Error 'version.ps1: could not read the version from CMakeLists.txt'
exit 1
