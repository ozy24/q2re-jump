<#
.SYNOPSIS
  Stage a throwaway q2repro game dir for the headless map scan.

.DESCRIPTION
  Creates <q2repro>\mapscan\ holding:
    maps\          a directory junction to the map corpus (no 2.9 GB copy)
    game_x64.dll   a copy of the mod's built DLL

  q2repro's GameDll_Load (src/common/gamedll.c:78-103) probes
  $sys_libdir/$fs_game/ for game_x64.dll, so "+set game mapscan" picks this up.

  q2repro\baseq2 is never touched -- its game_x64.dll.vanilla stays inactive.

.PARAMETER Remove
  Tear the scan dir down again. Removes the junction with rmdir (which does not
  follow it), so the corpus itself is never at risk.
#>
[CmdletBinding()]
param(
    [string]$Q2Repro = 'G:\Program Files (Legacy)\Quake 2\q2repro',
    [string]$MapsDir = 'E:\code\projects\q2re-jump\q2jump-maps',
    [string]$Dll,
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
if (-not $Dll) {
    # $PSScriptRoot is not reliably populated in param defaults under 5.1.
    $Dll = Join-Path $MyInvocation.MyCommand.Path '..\..\..\dist\game_x64.dll'
    $Dll = [System.IO.Path]::GetFullPath($Dll)
}
$scanDir = Join-Path $Q2Repro 'mapscan'
$mapsLink = Join-Path $scanDir 'maps'

if ($Remove) {
    if (Test-Path $mapsLink) {
        # rmdir on a junction removes the link only, never the target.
        & cmd /c rmdir "`"$mapsLink`""
        Write-Host "removed junction $mapsLink"
    }
    if (Test-Path $scanDir) {
        Remove-Item -Recurse -Force $scanDir
        Write-Host "removed $scanDir"
    }
    return
}

foreach ($p in @($Q2Repro, $MapsDir)) {
    if (-not (Test-Path $p)) { throw "missing required path: $p" }
}
if (-not (Test-Path $Dll)) {
    throw "mod DLL not found at $Dll -- run build.bat first"
}

$ded = Join-Path $Q2Repro 'q2reproded.exe'
if (-not (Test-Path $ded)) { throw "missing dedicated server: $ded" }

if (-not (Test-Path $scanDir)) {
    New-Item -ItemType Directory $scanDir | Out-Null
    Write-Host "created $scanDir"
}

if (Test-Path $mapsLink) {
    $item = Get-Item $mapsLink -Force
    if ($item.LinkType -ne 'Junction') {
        throw "$mapsLink exists and is not a junction -- refusing to touch it"
    }
    Write-Host "junction already present: $mapsLink -> $($item.Target)"
} else {
    & cmd /c mklink /J "`"$mapsLink`"" "`"$MapsDir`"" | Out-Null
    if (-not (Test-Path $mapsLink)) { throw "failed to create junction $mapsLink" }
    Write-Host "junction $mapsLink -> $MapsDir"
}

Copy-Item $Dll (Join-Path $scanDir 'game_x64.dll') -Force
$dllInfo = Get-Item (Join-Path $scanDir 'game_x64.dll')
Write-Host "copied game_x64.dll ($($dllInfo.Length) bytes, built $($dllInfo.LastWriteTime))"

# Recursive: sort_corpus.py may have filed the corpus into playable/, not-jump/
# and so on, in which case nothing sits at the top level.
$bspCount = @(Get-ChildItem $mapsLink -Recurse -Filter *.bsp -File).Count
$subdirs = @(Get-ChildItem $mapsLink -Directory)
Write-Host "$bspCount .bsp visible through the junction"
if ($subdirs.Count) {
    Write-Host "sorted into $($subdirs.Count) folder(s): maps must be loaded as '<folder>/<name>'"
}

# Sanity: baseq2 must still have no active game DLL, so a stray run can't
# silently test the wrong module.
$stray = Join-Path $Q2Repro 'baseq2\game_x64.dll'
if (Test-Path $stray) {
    Write-Warning "$stray exists -- baseq2 has an active game DLL, which is not expected"
}
Write-Host "`nready: q2reproded.exe +set game mapscan"
