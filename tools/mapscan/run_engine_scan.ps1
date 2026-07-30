<#
.SYNOPSIS
  Grind the map corpus through q2reproded.exe with the mod DLL and capture the
  console output for every map.

.DESCRIPTION
  Drives the server entirely from generated .cfg files -- stdin piping does not
  work, because Sys_RunConsole (src/windows/system.c:214-247) only reads input
  when it owns a real console.

  Maps are processed in chunks, one server process per chunk, each cfg ending in
  "quit". Chunk cfgs stay well under the 64 KB CMD_BUFFER_SIZE limit
  (inc/common/cmd.h:25); Cmd_ExecuteFile rejects anything larger with EFBIG.

  Two failure modes are handled, because neither can be caught from inside the
  engine:
    crash  the process dies mid-chunk (access violation in map or mod code)
    hang   no new map marker appears for -StallSeconds
  In both cases the offending map is recorded and the chunk resumes from the
  next entry, so one bad map cannot cost the whole run.

  Requires setup_scan_dir.ps1 to have been run first.

.PARAMETER Only
  Scan just these map names (smoke testing). Bypasses maps.csv.
#>
[CmdletBinding()]
param(
    [string]$Q2Repro      = 'G:\Program Files (Legacy)\Quake 2\q2repro',
    [string]$OutDir,
    [string]$JumpDataDir,
    [int]$ChunkSize       = 400,
    [int]$StallSeconds    = 90,
    [int]$Limit           = 0,
    [string[]]$Only       = @(),
    [switch]$Resume
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir)      { $OutDir      = Join-Path $here 'out' }
if (-not $JumpDataDir) { $JumpDataDir = Join-Path $OutDir 'jumpdata' }

$scanDir = Join-Path $Q2Repro 'mapscan'
$exe     = Join-Path $Q2Repro 'q2reproded.exe'
$rawDir  = Join-Path $OutDir 'engine_raw'

foreach ($p in @($exe, $scanDir, (Join-Path $scanDir 'game_x64.dll'), (Join-Path $scanDir 'maps'))) {
    if (-not (Test-Path $p)) { throw "missing $p -- run setup_scan_dir.ps1 first" }
}
New-Item -ItemType Directory -Force $rawDir, $JumpDataDir | Out-Null

# ---------------------------------------------------------------------------
# Work list
# ---------------------------------------------------------------------------

$skipped = @()

if ($Only.Count) {
    # -Only takes bare names and assumes a flat corpus, which is what smoke
    # testing wants.
    $names = @($Only | ForEach-Object { [pscustomobject]@{ name = $_; path = $_ } })
} else {
    $csv = Join-Path $OutDir 'maps.csv'
    if (-not (Test-Path $csv)) { throw "missing $csv -- run scan_bsp.py first" }
    $rows = Import-Csv $csv
    # Only real .bsp files can be reached by "map <name>"; the corpus's .tmp and
    # .bsp_old strays have no loadable extension.
    $names = @()
    foreach ($r in $rows) {
        if ($r.file -notmatch '\.bsp$') {
            $skipped += [pscustomobject]@{ name = $r.name; reason = 'not_bsp_extension' }
            continue
        }
        # A quoted argument survives most odd names, but " and ; would break the
        # command parser itself.
        if ($r.name -match '["
;]') {
            $skipped += [pscustomobject]@{ name = $r.name; reason = 'unquotable_name' }
            continue
        }
        # mappath carries any subfolder (sort_corpus.py files the corpus into
        # playable/, not-jump/ and so on). "map <stem>" stops resolving once the
        # maps move, so the command needs the path - but the log marker stays the
        # bare stem, or every downstream join would have to learn about folders.
        $path = if ($r.PSObject.Properties['mappath'] -and $r.mappath) { $r.mappath } else { $r.name }
        $names += [pscustomobject]@{ name = $r.name; path = $path }
    }
}
if ($Limit -gt 0) { $names = $names | Select-Object -First $Limit }

$doneFile = Join-Path $OutDir 'engine_done.txt'
if ($Resume -and (Test-Path $doneFile)) {
    $already = [System.Collections.Generic.HashSet[string]]::new(
        [string[]](Get-Content $doneFile), [System.StringComparer]::OrdinalIgnoreCase)
    $before = $names.Count
    $names = @($names | Where-Object { -not $already.Contains($_.name) })
    Write-Host "resuming: $($before - $names.Count) already done, $($names.Count) to go"
} elseif (-not $Resume) {
    Remove-Item $doneFile -ErrorAction SilentlyContinue
}

if (-not $names.Count) { Write-Host 'nothing to do'; return }
Write-Host "$($names.Count) maps to scan, $($skipped.Count) skipped, chunk size $ChunkSize"

$cli = @(
    '+set dedicated 1'
    '+set game mapscan'
    '+set logfile 0'
    '+set sys_exitonerror 1'   # else ERR_FATAL Sleep(INFINITE)s on an owned console
    '+set sv_allow_map 1'
    '+set maxclients 4'
    '+set deathmatch 1'
    '+set g_jump 1'
    '+set jump_debug 1'
    "+set jump_data_dir `"$JumpDataDir`""
) -join ' '

# ---------------------------------------------------------------------------
# Run one batch of maps in a single server process.
# Returns the name of the map it died on, or $null if the batch completed.
# ---------------------------------------------------------------------------
function Invoke-Batch {
    # [object[]] not [string[]]: each entry is a {name, path} pair, and a string
    # cast would silently flatten them to empty names.
    param([object[]]$Batch, [string]$CfgName, [string]$OutFile)

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($n in $Batch) {
        $lines.Add("echo ===MAPSCAN $($n.name)===")
        $lines.Add("map `"$($n.path)`"")
        $lines.Add('wait')
        $lines.Add('wait')
    }
    $lines.Add('echo ===MAPSCAN-DONE===')
    $lines.Add('quit')

    $cfgPath = Join-Path $scanDir $CfgName
    Set-Content -Path $cfgPath -Value $lines -Encoding ASCII
    $bytes = (Get-Item $cfgPath).Length
    if ($bytes -ge 65536) { throw "$CfgName is $bytes bytes, over the 64 KB cbuf limit" }

    $proc = Start-Process -FilePath $exe -WorkingDirectory $Q2Repro -NoNewWindow -PassThru `
        -ArgumentList "$cli +exec $CfgName" -RedirectStandardOutput $OutFile

    # Poll for progress rather than trusting a single big timeout: a stalled map
    # is indistinguishable from a slow chunk by elapsed time alone.
    $lastMarkerCount = -1
    $lastProgress = Get-Date
    while (-not $proc.HasExited) {
        Start-Sleep -Milliseconds 2000
        $count = 0
        try {
            $count = @(Select-String -Path $OutFile -Pattern '^===MAPSCAN ' -SimpleMatch:$false).Count
        } catch { }
        if ($count -ne $lastMarkerCount) {
            $lastMarkerCount = $count
            $lastProgress = Get-Date
        } elseif (((Get-Date) - $lastProgress).TotalSeconds -gt $StallSeconds) {
            Write-Warning "  stalled for $StallSeconds s -- killing"
            try { $proc.Kill() } catch { }
            $proc.WaitForExit(10000) | Out-Null
            break
        }
    }
    try { $proc.WaitForExit(10000) | Out-Null } catch { }

    $text = if (Test-Path $OutFile) { Get-Content $OutFile -Raw } else { '' }
    if ($text -match '===MAPSCAN-DONE===') { return $null }

    # Markers carry the bare name, so the victim is a name, not a path.
    $seen = @([regex]::Matches($text, '(?m)^===MAPSCAN (.+?)===\s*$') | ForEach-Object { $_.Groups[1].Value })
    if (-not $seen.Count) { return $Batch[0].name }   # died before the first map
    return $seen[-1]
}

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

$casualties = @()
$chunkIndex = 0
$queue = [System.Collections.Generic.Queue[object]]::new()
$names | ForEach-Object { $queue.Enqueue($_) }
$startedAt = Get-Date

while ($queue.Count) {
    $batch = @()
    while ($queue.Count -and $batch.Count -lt $ChunkSize) { $batch += $queue.Dequeue() }

    $chunkIndex++
    $tag = 'chunk{0:d3}' -f $chunkIndex
    $outFile = Join-Path $rawDir "$tag.txt"
    $elapsed = ((Get-Date) - $startedAt).ToString('hh\:mm\:ss')
    Write-Host ("[{0}] {1}: {2} maps ({3} queued)" -f $elapsed, $tag, $batch.Count, $queue.Count)

    $victim = Invoke-Batch -Batch $batch -CfgName "$tag.cfg" -OutFile $outFile

    if ($victim) {
        Write-Warning "  died on '$victim' -- resuming after it"
        $casualties += [pscustomobject]@{ name = $victim; chunk = $tag }
        # Push everything after the victim back onto the front of the queue.
        $idx = [array]::IndexOf(@($batch | ForEach-Object { $_.name }), $victim)
        $rest = if ($idx -ge 0 -and $idx + 1 -lt $batch.Count) { $batch[($idx + 1)..($batch.Count - 1)] } else { @() }
        $remaining = @($rest) + @($queue.ToArray())
        $queue.Clear()
        $remaining | ForEach-Object { $queue.Enqueue($_) }
        $done = if ($idx -gt 0) { $batch[0..($idx - 1)] } else { @() }
    } else {
        $done = $batch
    }
    if ($done.Count) { Add-Content -Path $doneFile -Value @($done | ForEach-Object { $_.name }) }
}

if ($skipped.Count) {
    $skipped | Export-Csv (Join-Path $OutDir 'engine_skipped.csv') -NoTypeInformation
}
if ($casualties.Count) {
    $casualties | Export-Csv (Join-Path $OutDir 'engine_casualties.csv') -NoTypeInformation
    Write-Warning "$($casualties.Count) map(s) killed the server -- see engine_casualties.csv"
}

$total = ((Get-Date) - $startedAt).ToString('hh\:mm\:ss')
Write-Host "`ndone in $total; raw output in $rawDir"
Write-Host "next: python tools\mapscan\parse_log.py"
