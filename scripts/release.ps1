# Bump VERSION + jump_version.h and stamp CHANGELOG.md for a tagged release.
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
$changelogPath = Join-Path $repoRoot "CHANGELOG.md"

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

function Read-CurrentVersion {
    if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
        Fail "VERSION file is missing."
    }
    $text = (Get-Content -LiteralPath $versionPath -Raw).Trim()
    if ($text -notmatch '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)$') {
        Fail "VERSION must be a single MAJOR.MINOR.PATCH line (got '$text')."
    }
    return [pscustomobject]@{
        Major = [int]$Matches.major
        Minor = [int]$Matches.minor
        Patch = [int]$Matches.patch
        Text  = "$($Matches.major).$($Matches.minor).$($Matches.patch)"
    }
}

function Get-NextVersion($Current, [string]$Mode) {
    switch ($Mode) {
        "major" { return "$($Current.Major + 1).0.0" }
        "minor" { return "$($Current.Major).$($Current.Minor + 1).0" }
        "patch" { return "$($Current.Major).$($Current.Minor).$($Current.Patch + 1)" }
        default { Fail "Unknown VersionMode '$Mode'." }
    }
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    $encoding = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Write-VersionFiles([string]$Target) {
    if ($Target -notmatch '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)$') {
        Fail "Target version must be MAJOR.MINOR.PATCH (got '$Target')."
    }
    $major = $Matches.major
    $minor = $Matches.minor
    $patch = $Matches.patch

    Write-Utf8NoBom $versionPath ($Target + "`n")

    if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) {
        Fail "src/jump/jump_version.h is missing."
    }
    $header = Get-Content -LiteralPath $headerPath -Raw
    $header = [regex]::Replace($header, '(?m)^(\s*#define\s+JUMP_VERSION_MAJOR\s+)\d+(\s*)$', "`${1}$major`${2}")
    $header = [regex]::Replace($header, '(?m)^(\s*#define\s+JUMP_VERSION_MINOR\s+)\d+(\s*)$', "`${1}$minor`${2}")
    $header = [regex]::Replace($header, '(?m)^(\s*#define\s+JUMP_VERSION_PATCH\s+)\d+(\s*)$', "`${1}$patch`${2}")

    if ($header -notmatch "(?m)^\s*#define\s+JUMP_VERSION_MAJOR\s+$major\s*$" -or
        $header -notmatch "(?m)^\s*#define\s+JUMP_VERSION_MINOR\s+$minor\s*$" -or
        $header -notmatch "(?m)^\s*#define\s+JUMP_VERSION_PATCH\s+$patch\s*$") {
        Fail "Could not update JUMP_VERSION_* macros in src/jump/jump_version.h."
    }

    Write-Utf8NoBom $headerPath ($header.TrimEnd() + "`n")
}

function Get-UnreleasedBody {
    if (-not (Test-Path -LiteralPath $changelogPath -PathType Leaf)) {
        Fail "CHANGELOG.md is missing."
    }

    $lines = @(Get-Content -LiteralPath $changelogPath)
    $unreleasedIndex = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## \[Unreleased\]\s*$') {
            $unreleasedIndex = $i
            break
        }
    }
    if ($unreleasedIndex -lt 0) {
        Fail "CHANGELOG.md must contain a '## [Unreleased]' section."
    }

    $nextHeaderIndex = $lines.Count
    for ($i = $unreleasedIndex + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## \[') {
            $nextHeaderIndex = $i
            break
        }
    }

    $bodyLines = New-Object System.Collections.Generic.List[string]
    if ($nextHeaderIndex -gt ($unreleasedIndex + 1)) {
        foreach ($line in $lines[($unreleasedIndex + 1)..($nextHeaderIndex - 1)]) {
            [void]$bodyLines.Add($line)
        }
    }

    while ($bodyLines.Count -gt 0 -and [string]::IsNullOrWhiteSpace($bodyLines[0])) {
        $bodyLines.RemoveAt(0)
    }
    while ($bodyLines.Count -gt 0 -and [string]::IsNullOrWhiteSpace($bodyLines[$bodyLines.Count - 1])) {
        $bodyLines.RemoveAt($bodyLines.Count - 1)
    }

    if ($bodyLines.Count -eq 0) {
        Fail "CHANGELOG.md [Unreleased] is empty. Add release notes before cutting a release."
    }

    return [pscustomobject]@{
        Lines           = $lines
        UnreleasedIndex = $unreleasedIndex
        NextHeaderIndex = $nextHeaderIndex
        BodyLines       = $bodyLines
    }
}

function Stamp-Changelog([string]$Target, $Parsed) {
    foreach ($line in $Parsed.Lines) {
        if ($line -match "^## \[$([regex]::Escape($Target))\]") {
            Fail "CHANGELOG.md already has a section for $Target."
        }
    }

    $date = Get-Date -Format "yyyy-MM-dd"
    $result = New-Object System.Collections.Generic.List[string]
    if ($Parsed.UnreleasedIndex -gt 0) {
        foreach ($line in $Parsed.Lines[0..($Parsed.UnreleasedIndex - 1)]) {
            [void]$result.Add($line)
        }
    }

    [void]$result.Add("## [Unreleased]")
    [void]$result.Add("")
    [void]$result.Add("## [$Target] - $date")
    [void]$result.Add("")
    foreach ($line in $Parsed.BodyLines) {
        [void]$result.Add($line)
    }
    [void]$result.Add("")

    if ($Parsed.NextHeaderIndex -lt $Parsed.Lines.Count) {
        foreach ($line in $Parsed.Lines[$Parsed.NextHeaderIndex..($Parsed.Lines.Count - 1)]) {
            [void]$result.Add($line)
        }
    }

    while ($result.Count -gt 0 -and [string]::IsNullOrWhiteSpace($result[$result.Count - 1])) {
        $result.RemoveAt($result.Count - 1)
    }

    Write-Utf8NoBom $changelogPath (($result -join "`n") + "`n")
}

$current = Read-CurrentVersion
if ($PSCmdlet.ParameterSetName -eq "Exact") {
    $target = $Version
    if ($target -eq $current.Text) {
        Fail "Target version $target is already the current VERSION."
    }
}
else {
    $target = Get-NextVersion $current $VersionMode
}

Write-Host "Current version : $($current.Text)"
Write-Host "Target version  : $target"
Write-Host ""

# Validate changelog before touching VERSION / the header.
$parsedChangelog = Get-UnreleasedBody

Write-VersionFiles $target
Stamp-Changelog $target $parsedChangelog

# Re-run the alignment check so a bad stamp fails immediately.
& (Join-Path $PSScriptRoot "check-version.ps1")
if ($LASTEXITCODE -ne 0) {
    Fail "Version check failed after release stamping."
}

Write-Host ""
Write-Host "Files updated. Finish the release with:"
Write-Host ""
Write-Host "  git add VERSION src/jump/jump_version.h CHANGELOG.md"
Write-Host "  git commit -m `"release: v$target`""
Write-Host "  git tag v$target"
Write-Host "  git push origin HEAD --tags"
Write-Host ""
exit 0
