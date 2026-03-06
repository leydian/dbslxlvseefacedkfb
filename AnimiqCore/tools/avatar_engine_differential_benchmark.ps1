param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$VSeeFaceObservationPath = "",
    [string]$OutputJson = ".\build\reports\avatar_differential_benchmark_summary.json",
    [string]$OutputTxt = ".\build\reports\avatar_differential_benchmark_summary.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    if (Test-Path $Path) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

if (-not (Test-Path $ManifestPath)) {
    throw "ManifestPath not found: $ManifestPath"
}
if (-not (Test-Path $AvatarToolPath)) {
    throw "AvatarToolPath not found: $AvatarToolPath"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$manifestAbs = Resolve-AbsolutePath -Path $ManifestPath -BaseDirectory $repoRoot
$avatarToolAbs = Resolve-AbsolutePath -Path $AvatarToolPath -BaseDirectory $repoRoot
$manifest = Get-Content -Path $manifestAbs -Raw | ConvertFrom-Json
if ($null -eq $manifest -or $null -eq $manifest.samples) {
    throw "manifest schema invalid: expected root.samples[]"
}

$vseeRowsById = @{}
if (-not [string]::IsNullOrWhiteSpace($VSeeFaceObservationPath)) {
    $obsAbs = Resolve-AbsolutePath -Path $VSeeFaceObservationPath -BaseDirectory $repoRoot
    if (-not (Test-Path $obsAbs)) {
        throw "VSeeFaceObservationPath not found: $obsAbs"
    }
    $obs = Get-Content -Path $obsAbs -Raw | ConvertFrom-Json
    if ($null -ne $obs -and $null -ne $obs.rows) {
        foreach ($row in $obs.rows) {
            $id = "$($row.id)".Trim()
            if (-not [string]::IsNullOrWhiteSpace($id)) {
                $vseeRowsById[$id] = $row
            }
        }
    }
}

$rows = [System.Collections.Generic.List[object]]::new()
$p0 = 0
$p1 = 0
$p2 = 0
$none = 0

foreach ($sample in $manifest.samples) {
    $id = "$($sample.id)".Trim()
    if ([string]::IsNullOrWhiteSpace($id)) {
        continue
    }
    $samplePathRaw = "$($sample.path)"
    $samplePathAbs = Resolve-AbsolutePath -Path $samplePathRaw -BaseDirectory $repoRoot
    if (-not (Test-Path $samplePathAbs)) {
        $rows.Add([PSCustomObject]@{
            id = $id
            sample_class = "$($sample.sample_class)"
            sample_path = $samplePathAbs
            animiq_load_ok = $false
            animiq_parser_stage = "missing-file"
            animiq_primary_error = "FILE_NOT_FOUND"
            animiq_critical_warning_count = 0
            animiq_render_visible = $false
            vseeface_load_ok = $null
            vseeface_render_visible = $null
            vseeface_crash_or_freeze = $null
            priority = "P0"
            reason = "sample_file_missing"
        })
        $p0++
        continue
    }

    $tmpJson = Join-Path $env:TEMP ("animiq_benchmark_" + [Guid]::NewGuid().ToString("N") + ".json")
    $rawOut = & $avatarToolAbs $samplePathAbs "--json-out=$tmpJson" 2>&1
    $exitCode = $LASTEXITCODE
    $animiq = $null
    if (Test-Path $tmpJson) {
        $animiq = Get-Content -Path $tmpJson -Raw | ConvertFrom-Json
        Remove-Item -Path $tmpJson -Force -ErrorAction SilentlyContinue
    }

    $animiqLoadOk = $false
    $animiqStage = "unknown"
    $animiqPrimary = "UNKNOWN"
    $animiqCritical = 0
    $animiqWarningCodeCount = 0
    $animiqRenderVisible = $false
    $animiqCompat = "unknown"
    if ($null -ne $animiq) {
        $animiqLoadOk = [bool]$animiq.loadSucceeded
        $animiqStage = "$($animiq.parserStage)"
        $animiqPrimary = "$($animiq.primaryError)"
        $animiqCompat = "$($animiq.compat)"
        $animiqCritical = [int]$animiq.counts.criticalWarningCount
        $animiqWarningCodeCount = [int]$animiq.counts.warningCodeCount
        $animiqRenderVisible = [bool]$animiq.renderVisibleHeuristic
    } else {
        $animiqPrimary = "ANIMIQ_JSON_MISSING"
        $animiqStage = "failed"
        $animiqLoadOk = $false
        $animiqRenderVisible = $false
    }

    $vseeRow = $null
    if ($vseeRowsById.ContainsKey($id)) {
        $vseeRow = $vseeRowsById[$id]
    }
    $vseeLoadOk = if ($null -ne $vseeRow) { [bool]$vseeRow.load_ok } else { $null }
    $vseeRenderVisible = if ($null -ne $vseeRow) { [bool]$vseeRow.render_visible } else { $null }
    $vseeCrash = if ($null -ne $vseeRow) { [bool]$vseeRow.crash_or_freeze } else { $null }

    $priority = "NONE"
    $reason = "none"

    if ($vseeLoadOk -eq $true -and ((-not $animiqLoadOk) -or $animiqStage -ne "runtime-ready" -or $animiqPrimary -ne "NONE")) {
        $priority = "P0"
        $reason = "vseeface_ok_animiq_load_or_parse_failed"
        $p0++
    } elseif ($vseeLoadOk -eq $true -and $animiqLoadOk -and ($animiqCompat -eq "partial" -or $animiqCritical -gt 0 -or -not $animiqRenderVisible)) {
        $priority = "P1"
        $reason = "animiq_quality_gap_after_load"
        $p1++
    } elseif ($animiqLoadOk -and $animiqWarningCodeCount -gt 5) {
        $priority = "P2"
        $reason = "warning_debt_high"
        $p2++
    } else {
        $none++
    }

    $rows.Add([PSCustomObject]@{
        id = $id
        sample_class = "$($sample.sample_class)"
        sample_path = $samplePathAbs
        animiq_load_ok = $animiqLoadOk
        animiq_parser_stage = $animiqStage
        animiq_primary_error = $animiqPrimary
        animiq_compat = $animiqCompat
        animiq_critical_warning_count = $animiqCritical
        animiq_warning_code_count = $animiqWarningCodeCount
        animiq_render_visible = $animiqRenderVisible
        animiq_exit_code = $exitCode
        vseeface_load_ok = $vseeLoadOk
        vseeface_render_visible = $vseeRenderVisible
        vseeface_crash_or_freeze = $vseeCrash
        priority = $priority
        reason = $reason
        raw_head = if ($rawOut.Count -gt 0) { "$($rawOut[0])" } else { "" }
    })
}

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    manifest_path = $manifestAbs
    avatar_tool_path = $avatarToolAbs
    vseeface_observation_path = if ([string]::IsNullOrWhiteSpace($VSeeFaceObservationPath)) { "" } else { (Resolve-AbsolutePath -Path $VSeeFaceObservationPath -BaseDirectory $repoRoot) }
    total = $rows.Count
    priority = [PSCustomObject]@{
        p0 = $p0
        p1 = $p1
        p2 = $p2
        none = $none
    }
    rows = $rows
}

$outputJsonAbs = Resolve-AbsolutePath -Path $OutputJson -BaseDirectory $repoRoot
$outputTxtAbs = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
$outDir = Split-Path -Parent $outputJsonAbs
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $outputJsonAbs -Encoding UTF8

$lines = @()
$lines += "Avatar Differential Benchmark Summary"
$lines += "GeneratedUTC: $($summary.generated_utc)"
$lines += "ManifestPath: $($summary.manifest_path)"
$lines += "AvatarToolPath: $($summary.avatar_tool_path)"
$lines += "VSeeFaceObservationPath: $($summary.vseeface_observation_path)"
$lines += "Total: $($summary.total)"
$lines += "Priority: P0=$($summary.priority.p0), P1=$($summary.priority.p1), P2=$($summary.priority.p2), NONE=$($summary.priority.none)"
$lines += ""
$lines += "Rows"
foreach ($row in $rows) {
    $lines += "- [$($row.priority)] $($row.id) class=$($row.sample_class)"
    $lines += "  animiq(load=$($row.animiq_load_ok), stage=$($row.animiq_parser_stage), primary=$($row.animiq_primary_error), compat=$($row.animiq_compat), critical=$($row.animiq_critical_warning_count), visible=$($row.animiq_render_visible))"
    $lines += "  vseeface(load=$($row.vseeface_load_ok), visible=$($row.vseeface_render_visible), crash=$($row.vseeface_crash_or_freeze))"
    $lines += "  reason=$($row.reason)"
}
$lines | Set-Content -Path $outputTxtAbs -Encoding UTF8

Write-Host "json=$outputJsonAbs"
Write-Host "txt=$outputTxtAbs"
