param(
    [string]$MetricsCsvPath = "",
    [string]$MetricsDir = ".\build\reports",
    [string]$SummaryPath = ".\build\reports\render_perf_gate_summary.txt",
    [string]$HistoryCsvPath = ".\build\reports\render_perf_gate_history.csv",
    [switch]$SkipHistoryAppend,
    [ValidateSet("realtime-stable", "legacy", "aggressive", "ultra-parity", "desktop-60", "desktop-30")]
    [string]$Profile = "realtime-stable",
    [int]$TargetFps = 0,
    [double]$MaxP95FrameMs = 20.0,
    [double]$MaxP99FrameMs = 28.0,
    [double]$MaxFrameDropRatio = 0.02,
    [double]$MinLiveTickSampleRatio = 0.0,
    [double]$MaxPrivateMb = 0.0,
    [double]$MaxWorkingSetMb = 0.0,
    [double]$DropFrameThresholdMs = 33.3,
    [int]$MinSamples = 120
)

$ErrorActionPreference = "Stop"

$profileDefaults = switch ($Profile) {
    "desktop-60" { @{ MaxP95FrameMs = 20.0; MaxP99FrameMs = 28.0; MaxFrameDropRatio = 0.02; DropFrameThresholdMs = 33.3 } }
    "desktop-30" { @{ MaxP95FrameMs = 38.0; MaxP99FrameMs = 50.0; MaxFrameDropRatio = 0.03; DropFrameThresholdMs = 50.0 } }
    "aggressive" { @{ MaxP95FrameMs = 16.7; MaxP99FrameMs = 24.0; MaxFrameDropRatio = 0.01 } }
    "ultra-parity" { @{ MaxP95FrameMs = 16.7; MaxP99FrameMs = 20.0; MaxFrameDropRatio = 0.01 } }
    "legacy" { @{ MaxP95FrameMs = 33.0; MaxP99FrameMs = 50.0; MaxFrameDropRatio = 0.05 } }
    default { @{ MaxP95FrameMs = 20.0; MaxP99FrameMs = 28.0; MaxFrameDropRatio = 0.02 } }
}

if (-not $PSBoundParameters.ContainsKey("MaxP95FrameMs")) {
    $MaxP95FrameMs = [double]$profileDefaults.MaxP95FrameMs
}
if (-not $PSBoundParameters.ContainsKey("MaxP99FrameMs")) {
    $MaxP99FrameMs = [double]$profileDefaults.MaxP99FrameMs
}
if (-not $PSBoundParameters.ContainsKey("MaxFrameDropRatio")) {
    $MaxFrameDropRatio = [double]$profileDefaults.MaxFrameDropRatio
}
if (-not $PSBoundParameters.ContainsKey("DropFrameThresholdMs") -and $profileDefaults.ContainsKey("DropFrameThresholdMs")) {
    $DropFrameThresholdMs = [double]$profileDefaults.DropFrameThresholdMs
}
if ($TargetFps -gt 0) {
    $frameBudget = 1000.0 / [double]$TargetFps
    if (-not $PSBoundParameters.ContainsKey("MaxP95FrameMs")) {
        $MaxP95FrameMs = [Math]::Round(($frameBudget * 1.2), 3)
    }
    if (-not $PSBoundParameters.ContainsKey("MaxP99FrameMs")) {
        $MaxP99FrameMs = [Math]::Round(($frameBudget * 1.7), 3)
    }
    if (-not $PSBoundParameters.ContainsKey("DropFrameThresholdMs")) {
        $DropFrameThresholdMs = [Math]::Round(($frameBudget * 2.0), 3)
    }
}

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-Percentile {
    param([double[]]$SortedValues, [double]$Percent)
    if ($SortedValues.Count -eq 0) { return 0.0 }
    if ($SortedValues.Count -eq 1) { return $SortedValues[0] }
    $rank = ($Percent / 100.0) * ($SortedValues.Count - 1)
    $lower = [int][Math]::Floor($rank)
    $upper = [int][Math]::Ceiling($rank)
    if ($lower -eq $upper) { return $SortedValues[$lower] }
    $weight = $rank - $lower
    return $SortedValues[$lower] + (($SortedValues[$upper] - $SortedValues[$lower]) * $weight)
}

function Resolve-MetricsCsv {
    param([string]$InputPath, [string]$SearchDir)
    if (-not [string]::IsNullOrWhiteSpace($InputPath)) {
        return $InputPath
    }
    if (-not (Test-Path $SearchDir)) { return "" }
    $candidate = Get-ChildItem -Path $SearchDir -File -Filter "metrics_*.csv" | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if ($null -eq $candidate) { return "" }
    return $candidate.FullName
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedMetricsDir = Resolve-AbsolutePath -Path $MetricsDir -BaseDirectory $repoRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
$resolvedHistoryCsv = Resolve-AbsolutePath -Path $HistoryCsvPath -BaseDirectory $repoRoot
$inputPath = if ([string]::IsNullOrWhiteSpace($MetricsCsvPath)) { "" } else { Resolve-AbsolutePath -Path $MetricsCsvPath -BaseDirectory $repoRoot }
$resolvedCsv = Resolve-MetricsCsv -InputPath $inputPath -SearchDir $resolvedMetricsDir
if ([string]::IsNullOrWhiteSpace($resolvedCsv) -or -not (Test-Path $resolvedCsv)) {
    throw "metrics csv not found. MetricsCsvPath='$MetricsCsvPath', MetricsDir='$resolvedMetricsDir'"
}

$rows = Import-Csv -Path $resolvedCsv
if ($rows.Count -lt $MinSamples) {
    throw "insufficient samples: $($rows.Count) (required >= $MinSamples)"
}

$hasMeasurementSourceColumn = $false
$hasMeasurementSessionColumn = $false
$hasMemorySampleStatusColumn = $false
if ($rows.Count -gt 0) {
    $firstRowProperties = @($rows[0].PSObject.Properties.Name)
    $hasMeasurementSourceColumn = $firstRowProperties -contains "measurement_source"
    $hasMeasurementSessionColumn = $firstRowProperties -contains "measurement_session_id"
    $hasMemorySampleStatusColumn = $firstRowProperties -contains "memory_sample_status"
}

$values = [System.Collections.Generic.List[double]]::new()
$privateValues = [System.Collections.Generic.List[double]]::new()
$workingSetValues = [System.Collections.Generic.List[double]]::new()
$liveTickSamples = 0
$otherSourceSamples = 0
$unknownSourceSamples = 0
$memorySampleOkCount = 0
$memorySampleStaleCount = 0
$memorySampleFailedCount = 0
$memorySampleUnknownCount = 0
$measurementSessions = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($r in $rows) {
    $v = 0.0
    if ([double]::TryParse("$($r.frame_ms)", [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$v)) {
        $values.Add($v)
    }

    $pv = 0.0
    if ([double]::TryParse("$($r.private_mb)", [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$pv)) {
        $privateValues.Add($pv)
    }

    $wv = 0.0
    if ([double]::TryParse("$($r.working_set_mb)", [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$wv)) {
        $workingSetValues.Add($wv)
    }

    if ($hasMeasurementSourceColumn) {
        $source = "$($r.measurement_source)".Trim().ToLowerInvariant()
        if ($source -eq "live_tick") {
            $liveTickSamples++
        } elseif ([string]::IsNullOrWhiteSpace($source)) {
            $unknownSourceSamples++
        } else {
            $otherSourceSamples++
        }
    }

    if ($hasMeasurementSessionColumn) {
        $sessionId = "$($r.measurement_session_id)".Trim()
        if (-not [string]::IsNullOrWhiteSpace($sessionId)) {
            $null = $measurementSessions.Add($sessionId)
        }
    }

    if ($hasMemorySampleStatusColumn) {
        $memoryStatus = "$($r.memory_sample_status)".Trim().ToLowerInvariant()
        switch ($memoryStatus) {
            "ok" { $memorySampleOkCount++ }
            "stale" { $memorySampleStaleCount++ }
            "failed" { $memorySampleFailedCount++ }
            default { $memorySampleUnknownCount++ }
        }
    }
}
if ($values.Count -lt $MinSamples) {
    throw "insufficient numeric frame_ms values: $($values.Count) (required >= $MinSamples)"
}

$sorted = @($values | Sort-Object)
$p50 = Get-Percentile -SortedValues $sorted -Percent 50
$p95 = Get-Percentile -SortedValues $sorted -Percent 95
$p99 = Get-Percentile -SortedValues $sorted -Percent 99
$avg = ($sorted | Measure-Object -Average).Average
$max = ($sorted | Measure-Object -Maximum).Maximum
$dropCount = @($sorted | Where-Object { $_ -gt $DropFrameThresholdMs }).Count
$dropRatio = [Math]::Round(($dropCount / [double]$sorted.Count), 6)
if ($hasMeasurementSourceColumn) {
    $liveTickRatio = [Math]::Round(($liveTickSamples / [double]$sorted.Count), 6)
} else {
    $liveTickRatio = 0.0
}
$avgPrivateMb = if ($privateValues.Count -gt 0) { ($privateValues | Measure-Object -Average).Average } else { 0.0 }
$p95PrivateMb = if ($privateValues.Count -gt 0) { Get-Percentile -SortedValues @($privateValues | Sort-Object) -Percent 95 } else { 0.0 }
$avgWorkingSetMb = if ($workingSetValues.Count -gt 0) { ($workingSetValues | Measure-Object -Average).Average } else { 0.0 }
$p95WorkingSetMb = if ($workingSetValues.Count -gt 0) { Get-Percentile -SortedValues @($workingSetValues | Sort-Object) -Percent 95 } else { 0.0 }

$gateP95 = $p95 -le $MaxP95FrameMs
$gateP99 = $p99 -le $MaxP99FrameMs
$gateDrop = $dropRatio -le $MaxFrameDropRatio
$gateLiveTick = ($MinLiveTickSampleRatio -le 0.0) -or (-not $hasMeasurementSourceColumn) -or ($liveTickRatio -ge $MinLiveTickSampleRatio)
$gatePrivate = ($MaxPrivateMb -le 0.0) -or ($privateValues.Count -eq 0) -or ($p95PrivateMb -le $MaxPrivateMb)
$gateWorkingSet = ($MaxWorkingSetMb -le 0.0) -or ($workingSetValues.Count -eq 0) -or ($p95WorkingSetMb -le $MaxWorkingSetMb)
$overall = $gateP95 -and $gateP99 -and $gateDrop -and $gateLiveTick -and $gatePrivate -and $gateWorkingSet

$summaryLines = [System.Collections.Generic.List[string]]::new()
$summaryLines.Add("Render Performance Gate Summary")
$summaryLines.Add("Generated: $(Get-Date -Format o)")
$summaryLines.Add("MetricsCsv: $resolvedCsv")
$summaryLines.Add("Profile: $Profile")
$summaryLines.Add("TargetFps: $TargetFps")
$summaryLines.Add("SampleCount: $($sorted.Count)")
$summaryLines.Add("AvgFrameMs: $([Math]::Round($avg, 3))")
$summaryLines.Add("P50FrameMs: $([Math]::Round($p50, 3))")
$summaryLines.Add("P95FrameMs: $([Math]::Round($p95, 3))")
$summaryLines.Add("P99FrameMs: $([Math]::Round($p99, 3))")
$summaryLines.Add("MaxFrameMs: $([Math]::Round($max, 3))")
$summaryLines.Add("DropFrameThresholdMs: $DropFrameThresholdMs")
$summaryLines.Add("DropFrameCount: $dropCount")
$summaryLines.Add("DropFrameRatio: $dropRatio")
$summaryLines.Add("LiveTickSampleRatio: $liveTickRatio")
$summaryLines.Add("AvgPrivateMb: $([Math]::Round($avgPrivateMb, 3))")
$summaryLines.Add("P95PrivateMb: $([Math]::Round($p95PrivateMb, 3))")
$summaryLines.Add("AvgWorkingSetMb: $([Math]::Round($avgWorkingSetMb, 3))")
$summaryLines.Add("P95WorkingSetMb: $([Math]::Round($p95WorkingSetMb, 3))")
$summaryLines.Add("MeasurementSourceColumnPresent: $hasMeasurementSourceColumn")
$summaryLines.Add("MeasurementSessionColumnPresent: $hasMeasurementSessionColumn")
$summaryLines.Add("MemorySampleStatusColumnPresent: $hasMemorySampleStatusColumn")
$summaryLines.Add("LiveTickSamples: $liveTickSamples")
$summaryLines.Add("OtherSourceSamples: $otherSourceSamples")
$summaryLines.Add("UnknownSourceSamples: $unknownSourceSamples")
$summaryLines.Add("MeasurementSessionCount: $($measurementSessions.Count)")
$summaryLines.Add("MemorySampleOkCount: $memorySampleOkCount")
$summaryLines.Add("MemorySampleStaleCount: $memorySampleStaleCount")
$summaryLines.Add("MemorySampleFailedCount: $memorySampleFailedCount")
$summaryLines.Add("MemorySampleUnknownCount: $memorySampleUnknownCount")
$summaryLines.Add("")
$summaryLines.Add("Gate Results")
$summaryLines.Add("- GateP95 (p95 <= $MaxP95FrameMs): $(if ($gateP95) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- GateP99 (p99 <= $MaxP99FrameMs): $(if ($gateP99) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- GateDrop (drop_ratio <= $MaxFrameDropRatio): $(if ($gateDrop) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- GateLiveTick (live_tick_ratio >= $MinLiveTickSampleRatio, disabled when <=0 or no column): $(if ($gateLiveTick) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- GatePrivate (p95_private_mb <= $MaxPrivateMb, disabled when <=0 or no column): $(if ($gatePrivate) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- GateWorkingSet (p95_working_set_mb <= $MaxWorkingSetMb, disabled when <=0 or no column): $(if ($gateWorkingSet) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- Overall: $(if ($overall) { 'PASS' } else { 'FAIL' })")

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null
$summaryLines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"

if (-not $SkipHistoryAppend) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedHistoryCsv) | Out-Null
    $header = "generated_utc,metrics_csv,profile,target_fps,sample_count,p95_ms,p99_ms,drop_ratio,p95_private_mb,p95_working_set_mb,live_tick_ratio,memory_sample_failed_count,overall"
    if (-not (Test-Path -LiteralPath $resolvedHistoryCsv)) {
        $header | Set-Content -Path $resolvedHistoryCsv -Encoding UTF8
    }
    $row = @(
        (Get-Date).ToUniversalTime().ToString("s"),
        $resolvedCsv,
        $Profile,
        $TargetFps,
        $sorted.Count,
        ([Math]::Round($p95, 3)),
        ([Math]::Round($p99, 3)),
        $dropRatio,
        ([Math]::Round($p95PrivateMb, 3)),
        ([Math]::Round($p95WorkingSetMb, 3)),
        $liveTickRatio,
        $memorySampleFailedCount,
        (if ($overall) { "PASS" } else { "FAIL" })
    )
    ($row -join ",") | Add-Content -Path $resolvedHistoryCsv -Encoding UTF8
    Write-Host "history_csv=$resolvedHistoryCsv"
}

if (-not $overall) {
    exit 1
}
