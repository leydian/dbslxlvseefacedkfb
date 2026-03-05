param(
    [string]$MetricsCsvPath = "",
    [string]$MetricsDir = ".\build\reports",
    [string]$SummaryPath = ".\build\reports\render_perf_gate_summary.txt",
    [ValidateSet("realtime-stable", "legacy", "aggressive", "ultra-parity")]
    [string]$Profile = "realtime-stable",
    [double]$MaxP95FrameMs = 20.0,
    [double]$MaxP99FrameMs = 28.0,
    [double]$MaxFrameDropRatio = 0.02,
    [double]$DropFrameThresholdMs = 33.3,
    [int]$MinSamples = 120
)

$ErrorActionPreference = "Stop"

$profileDefaults = switch ($Profile) {
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
$inputPath = if ([string]::IsNullOrWhiteSpace($MetricsCsvPath)) { "" } else { Resolve-AbsolutePath -Path $MetricsCsvPath -BaseDirectory $repoRoot }
$resolvedCsv = Resolve-MetricsCsv -InputPath $inputPath -SearchDir $resolvedMetricsDir
if ([string]::IsNullOrWhiteSpace($resolvedCsv) -or -not (Test-Path $resolvedCsv)) {
    throw "metrics csv not found. MetricsCsvPath='$MetricsCsvPath', MetricsDir='$resolvedMetricsDir'"
}

$rows = Import-Csv -Path $resolvedCsv
if ($rows.Count -lt $MinSamples) {
    throw "insufficient samples: $($rows.Count) (required >= $MinSamples)"
}

$values = [System.Collections.Generic.List[double]]::new()
foreach ($r in $rows) {
    $v = 0.0
    if ([double]::TryParse("$($r.frame_ms)", [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$v)) {
        $values.Add($v)
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

$gateP95 = $p95 -le $MaxP95FrameMs
$gateP99 = $p99 -le $MaxP99FrameMs
$gateDrop = $dropRatio -le $MaxFrameDropRatio
$overall = $gateP95 -and $gateP99 -and $gateDrop

$summaryLines = [System.Collections.Generic.List[string]]::new()
$summaryLines.Add("Render Performance Gate Summary")
$summaryLines.Add("Generated: $(Get-Date -Format o)")
$summaryLines.Add("MetricsCsv: $resolvedCsv")
$summaryLines.Add("Profile: $Profile")
$summaryLines.Add("SampleCount: $($sorted.Count)")
$summaryLines.Add("AvgFrameMs: $([Math]::Round($avg, 3))")
$summaryLines.Add("P50FrameMs: $([Math]::Round($p50, 3))")
$summaryLines.Add("P95FrameMs: $([Math]::Round($p95, 3))")
$summaryLines.Add("P99FrameMs: $([Math]::Round($p99, 3))")
$summaryLines.Add("MaxFrameMs: $([Math]::Round($max, 3))")
$summaryLines.Add("DropFrameThresholdMs: $DropFrameThresholdMs")
$summaryLines.Add("DropFrameCount: $dropCount")
$summaryLines.Add("DropFrameRatio: $dropRatio")
$summaryLines.Add("")
$summaryLines.Add("Gate Results")
$summaryLines.Add("- GateP95 (p95 <= $MaxP95FrameMs): $(if ($gateP95) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- GateP99 (p99 <= $MaxP99FrameMs): $(if ($gateP99) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- GateDrop (drop_ratio <= $MaxFrameDropRatio): $(if ($gateDrop) { 'PASS' } else { 'FAIL' })")
$summaryLines.Add("- Overall: $(if ($overall) { 'PASS' } else { 'FAIL' })")

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null
$summaryLines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"

if (-not $overall) {
    exit 1
}
