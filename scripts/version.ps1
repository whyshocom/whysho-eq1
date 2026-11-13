<#
.SYNOPSIS
    Derive the project version for Whysho EQ1 packaging (PowerShell equivalent of version.sh).

.DESCRIPTION
    Resolution order:
      1. $env:WHYSHO_VERSION, if set.
      2. The current git tag, if HEAD is exactly on a `vX.Y.Z` tag.
      3. The most recent `vX.Y.Z` tag reachable from HEAD.
      4. The fallback default `0.1.0`.

    A leading `v` is stripped so the output is a bare semantic version
    (e.g. `1.2.3`). The resolved version string is written to stdout.

.EXAMPLE
    $version = & scripts/version.ps1
#>

$ErrorActionPreference = 'Stop'

$DefaultVersion = '0.1.0'

function Remove-VPrefix {
    param([string]$Value)
    return ($Value -replace '^[vV]', '')
}

# 1. Explicit override via environment.
if ($env:WHYSHO_VERSION) {
    Write-Output (Remove-VPrefix $env:WHYSHO_VERSION)
    exit 0
}

# 2 & 3. Try git. Any failure falls through to the default.
$gitAvailable = $null -ne (Get-Command git -ErrorAction SilentlyContinue)
if ($gitAvailable) {
    try {
        $null = git rev-parse --git-dir 2>$null
        if ($LASTEXITCODE -eq 0) {
            # Exact tag on HEAD?
            $exactTag = git describe --tags --exact-match 2>$null
            if ($LASTEXITCODE -eq 0 -and $exactTag -and ($exactTag -match '^[vV]')) {
                Write-Output (Remove-VPrefix $exactTag)
                exit 0
            }

            # Most recent v* tag reachable from HEAD.
            $nearestTag = git describe --tags --abbrev=0 --match 'v*' 2>$null
            if ($LASTEXITCODE -eq 0 -and $nearestTag) {
                Write-Output (Remove-VPrefix $nearestTag)
                exit 0
            }
        }
    } catch {
        # Ignore and fall through to default.
    }
}

# 4. Fallback.
Write-Output $DefaultVersion
