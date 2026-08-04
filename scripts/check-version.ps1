# Verify VERSION, jump_version.h, and CHANGELOG.md stay aligned.
# Run from repo root, or via build.bat. Exit 0 on success, 1 on failure.
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$versionPath = Join-Path $repoRoot "VERSION"
$headerPath = Join-Path $repoRoot "src\jump\jump_version.h"
$changelogPath = Join-Path $repoRoot "CHANGELOG.md"

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    Fail "VERSION file is missing."
}
if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) {
    Fail "src/jump/jump_version.h is missing."
}
if (-not (Test-Path -LiteralPath $changelogPath -PathType Leaf)) {
    Fail "CHANGELOG.md is missing."
}

$versionText = (Get-Content -LiteralPath $versionPath -Raw).Trim()
if ($versionText -notmatch '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)$') {
    Fail "VERSION must be a single MAJOR.MINOR.PATCH line (got '$versionText')."
}

$major = [int]$Matches.major
$minor = [int]$Matches.minor
$patch = [int]$Matches.patch
$version = "$major.$minor.$patch"

$header = Get-Content -LiteralPath $headerPath -Raw

function Read-Macro([string]$Name) {
    $pattern = "(?m)^\s*#define\s+$Name\s+(\d+)\s*$"
    $m = [regex]::Match($header, $pattern)
    if (-not $m.Success) {
        Fail "Could not find #define $Name in src/jump/jump_version.h."
    }
    return [int]$m.Groups[1].Value
}

$headerMajor = Read-Macro "JUMP_VERSION_MAJOR"
$headerMinor = Read-Macro "JUMP_VERSION_MINOR"
$headerPatch = Read-Macro "JUMP_VERSION_PATCH"

if ($headerMajor -ne $major -or $headerMinor -ne $minor -or $headerPatch -ne $patch) {
    Fail ("VERSION ($version) does not match jump_version.h " +
        "($headerMajor.$headerMinor.$headerPatch).")
}

if ($header -notmatch '(?m)^\s*#define\s+JUMP_VERSION_STRING\s+JUMP_VER_STR\s*\(') {
    Fail "jump_version.h must derive JUMP_VERSION_STRING from JUMP_VER_STR(...)."
}

$changelog = Get-Content -LiteralPath $changelogPath -Raw
if ($changelog -notmatch '(?m)^## \[Unreleased\]\s*$') {
    Fail "CHANGELOG.md must contain a '## [Unreleased]' section."
}

# The current VERSION deliberately does NOT need a dated section. Notes accumulate
# under [Unreleased] across however many bumps happen during development, and only
# a release stamps them under the version being released. Requiring the section here
# would force every bump to consume the notes early, which is the opposite of an
# ongoing changelog.

Write-Host "[OK] Version check passed ($version)."
exit 0
