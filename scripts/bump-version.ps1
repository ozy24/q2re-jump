# Bump VERSION + src/jump/jump_version.h. Nothing else.
#
# CHANGELOG.md is deliberately untouched: notes stay under [Unreleased] and
# accumulate across however many bumps happen during development, so that cutting a
# release later can list every change since the last one. scripts/release.ps1 is what
# stamps them under a dated version heading.
#
# Updates files only — does not commit, tag, or push.
[CmdletBinding(DefaultParameterSetName = "Mode")]
param(
    [Parameter(ParameterSetName = "Mode")]
    [ValidateSet("major", "minor", "patch")]
    [string]$VersionMode = "patch",

    [Parameter(ParameterSetName = "Exact", Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$versionPath = Join-Path $repoRoot "VERSION"
$headerPath = Join-Path $repoRoot "src\jump\jump_version.h"

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    $encoding = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Read-Utf8Text([string]$Path) {
    $encoding = New-Object System.Text.UTF8Encoding $false
    return [System.IO.File]::ReadAllText($Path, $encoding)
}

if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    Fail "VERSION file is missing."
}
if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) {
    Fail "src/jump/jump_version.h is missing."
}

$currentText = (Read-Utf8Text $versionPath).Trim()
if ($currentText -notmatch '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)$') {
    Fail "VERSION must be a single MAJOR.MINOR.PATCH line (got '$currentText')."
}

$curMajor = [int]$Matches.major
$curMinor = [int]$Matches.minor
$curPatch = [int]$Matches.patch

if ($PSCmdlet.ParameterSetName -eq "Exact") {
    $target = $Version
    if ($target -eq $currentText) {
        Fail "VERSION is already $target."
    }
}
else {
    switch ($VersionMode) {
        "major" { $target = "$($curMajor + 1).0.0" }
        "minor" { $target = "$curMajor.$($curMinor + 1).0" }
        "patch" { $target = "$curMajor.$curMinor.$($curPatch + 1)" }
        default { Fail "Unknown VersionMode '$VersionMode'." }
    }
}

if ($target -notmatch '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)$') {
    Fail "Target version must be MAJOR.MINOR.PATCH (got '$target')."
}
$major = $Matches.major
$minor = $Matches.minor
$patch = $Matches.patch

Write-Host "Current version : $currentText"
Write-Host "Target version  : $target"
Write-Host ""

Write-Utf8NoBom $versionPath ($target + "`n")

$header = Read-Utf8Text $headerPath
$header = [regex]::Replace($header, '(?m)^(\s*#define\s+JUMP_VERSION_MAJOR\s+)\d+(\s*)$', "`${1}$major`${2}")
$header = [regex]::Replace($header, '(?m)^(\s*#define\s+JUMP_VERSION_MINOR\s+)\d+(\s*)$', "`${1}$minor`${2}")
$header = [regex]::Replace($header, '(?m)^(\s*#define\s+JUMP_VERSION_PATCH\s+)\d+(\s*)$', "`${1}$patch`${2}")

if ($header -notmatch "(?m)^\s*#define\s+JUMP_VERSION_MAJOR\s+$major\s*$" -or
    $header -notmatch "(?m)^\s*#define\s+JUMP_VERSION_MINOR\s+$minor\s*$" -or
    $header -notmatch "(?m)^\s*#define\s+JUMP_VERSION_PATCH\s+$patch\s*$") {
    Fail "Could not update JUMP_VERSION_* macros in src/jump/jump_version.h."
}

Write-Utf8NoBom $headerPath ($header.TrimEnd() + "`n")

& (Join-Path $PSScriptRoot "check-version.ps1")
if ($LASTEXITCODE -ne 0) {
    Fail "Version check failed after the bump."
}

Write-Host ""
Write-Host "Bumped to $target. Commit with:"
Write-Host ""
Write-Host "  git add VERSION src/jump/jump_version.h"
Write-Host "  git commit -m `"chore: bump version to $target`""
Write-Host ""
exit 0
