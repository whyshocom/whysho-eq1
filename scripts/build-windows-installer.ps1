<#
.SYNOPSIS
    Build the whysho EQ1 Windows installer (.exe) with Inno Setup (ISCC).

.DESCRIPTION
    Resolves the version (scripts/version.ps1 unless -Version is given), locates
    the ISCC compiler, and compiles packaging/windows/installer.iss into
    dist/WhyshoEQ1-<version>-Windows.exe.

    Optionally code-signs the Standalone .exe and the finished installer with
    signtool when the signing environment variables are present (see below);
    when they are absent the step is skipped with a warning so unsigned local
    and CI builds still succeed.

    Signing environment variables (all optional, all required together):
      WINDOWS_CERT_PFX_BASE64  Base64 of the EV / code-signing .pfx certificate.
      WINDOWS_CERT_PWD         Password for the .pfx.
      WINDOWS_CERT_TIMESTAMP   RFC3161 timestamp URL
                               (default: http://timestamp.digicert.com).

.PARAMETER Version
    Override the version string (otherwise derived from git tags / default).

.PARAMETER BuildDir
    The CMake build directory containing WhyshoEQ1_artefacts (default: ./build).

.PARAMETER OutputDir
    Directory for the finished installer (default: ./dist).

.EXAMPLE
    pwsh scripts/build-windows-installer.ps1
    pwsh scripts/build-windows-installer.ps1 -Version 1.2.3 -BuildDir build
#>

[CmdletBinding()]
param(
    [string]$Version,
    [string]$BuildDir,
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --- Resolve repo-relative paths (robust to being run from anywhere) ----------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir '..') | Select-Object -ExpandProperty Path

if (-not $Version) {
    $Version = (& (Join-Path $ScriptDir 'version.ps1')).Trim()
}
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot 'build'
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $RepoRoot 'dist'
}

# Normalize to absolute paths for ISCC defines.
$BuildDir  = [System.IO.Path]::GetFullPath($BuildDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$IssFile   = Join-Path $RepoRoot 'packaging\windows\installer.iss'

Write-Host "==> whysho EQ1 Windows installer"
Write-Host "    Version   : $Version"
Write-Host "    RepoRoot  : $RepoRoot"
Write-Host "    BuildDir  : $BuildDir"
Write-Host "    OutputDir : $OutputDir"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# --- Sanity-check expected artefacts ------------------------------------------
$ArtefactsDir  = Join-Path $BuildDir 'WhyshoEQ1_artefacts\Release'
$Vst3Bundle    = Join-Path $ArtefactsDir 'VST3\whysho EQ1.vst3'
$StandaloneExe = Join-Path $ArtefactsDir 'Standalone\whysho EQ1.exe'

foreach ($p in @($Vst3Bundle, $StandaloneExe)) {
    if (-not (Test-Path $p)) {
        throw "Expected build artefact not found: $p`nBuild the plugin first (cmake --build build --config Release)."
    }
}

# --- Locate ISCC --------------------------------------------------------------
function Find-ISCC {
    $cmd = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    throw "ISCC.exe (Inno Setup 6) not found. Install it, e.g.: choco install innosetup"
}
$Iscc = Find-ISCC
Write-Host "    ISCC      : $Iscc"

# --- Optional: code-sign the Standalone .exe before packaging -----------------
$SignToolArgs = $null
$TempPfx = $null
if ($env:WINDOWS_CERT_PFX_BASE64 -and $env:WINDOWS_CERT_PWD) {
    $signtool = Get-Command 'signtool.exe' -ErrorAction SilentlyContinue
    if (-not $signtool) {
        Write-Warning "WINDOWS_CERT_* secrets present but signtool.exe not found on PATH; skipping signing."
    } else {
        $timestamp = if ($env:WINDOWS_CERT_TIMESTAMP) { $env:WINDOWS_CERT_TIMESTAMP } else { 'http://timestamp.digicert.com' }
        $TempPfx = Join-Path ([System.IO.Path]::GetTempPath()) ("whysho-cert-{0}.pfx" -f ([guid]::NewGuid()))
        [System.IO.File]::WriteAllBytes($TempPfx, [System.Convert]::FromBase64String($env:WINDOWS_CERT_PFX_BASE64))
        $SignToolArgs = @(
            'sign', '/fd', 'SHA256',
            '/f', $TempPfx, '/p', $env:WINDOWS_CERT_PWD,
            '/tr', $timestamp, '/td', 'SHA256'
        )
        Write-Host "==> Signing Standalone executable"
        & $signtool.Source @SignToolArgs "$StandaloneExe"
        if ($LASTEXITCODE -ne 0) { throw "signtool failed on Standalone exe (exit $LASTEXITCODE)" }
    }
} else {
    Write-Warning "Code-signing secrets (WINDOWS_CERT_PFX_BASE64 / WINDOWS_CERT_PWD) not set; building UNSIGNED installer."
}

# --- Compile the installer ----------------------------------------------------
Write-Host "==> Compiling installer with ISCC"
$isccArgs = @(
    "/DAppVersion=$Version",
    "/DRepoRoot=$RepoRoot",
    "/DBuildDir=$BuildDir",
    "/DOutputDir=$OutputDir",
    "/DLicenseFile=$(Join-Path $RepoRoot 'packaging\EULA.txt')",
    $IssFile
)
& $Iscc @isccArgs
if ($LASTEXITCODE -ne 0) { throw "ISCC failed (exit $LASTEXITCODE)" }

$Installer = Join-Path $OutputDir "WhyshoEQ1-$Version-Windows.exe"
if (-not (Test-Path $Installer)) {
    throw "Installer was not produced at expected path: $Installer"
}

# --- Optional: sign the finished installer ------------------------------------
if ($SignToolArgs) {
    $signtool = (Get-Command 'signtool.exe').Source
    Write-Host "==> Signing installer"
    & $signtool @SignToolArgs "$Installer"
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on installer (exit $LASTEXITCODE)" }
}

# --- Cleanup temp cert --------------------------------------------------------
if ($TempPfx -and (Test-Path $TempPfx)) {
    Remove-Item -Force $TempPfx
}

Write-Host ""
Write-Host "==> Done. Installer: $Installer"
