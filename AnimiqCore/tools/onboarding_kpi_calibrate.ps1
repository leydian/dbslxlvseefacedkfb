param(
    [string]$TelemetryPath = ".\build\reports\telemetry_latest.json",
    [double]$FloorThresholdPct = 70.0,
    [double]$TargetPercentile = 0.25,
    [int]$MinSessionCountFloor = 5,
    [string]$OutputJsonPath = ".\build\reports\onboarding_kpi_calibration.json",
    [string]$OutputTxtPath = ".\build\reports\onboarding_kpi_calibration.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-Percentile {
    param([double[]]$Values, [double]$Percentile)
    if ($null -eq $Values -or $Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $p = [Math]::Max(0.0, [Math]::Min(1.0, $Percentile))
    $index = [int][Math]::Floor(($sorted.Count - 1) * $p)
    return [double]$sorted[$index]
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedTelemetry = Resolve-AbsolutePath -Path $TelemetryPath -BaseDirectory $repoRoot
$resolvedOutputJson = Resolve-AbsolutePath -Path $OutputJsonPath -BaseDirectory $repoRoot
$resolvedOutputTxt = Resolve-AbsolutePath -Path $OutputTxtPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputJson) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputTxt) | Out-Null

if (-not (Test-Path -LiteralPath $resolvedTelemetry)) {
    throw "Telemetry file not found: $resolvedTelemetry"
}

$raw = Get-Content -Raw -Path $resolvedTelemetry | ConvertFrom-Json
$sessions = @()
if ($null -ne $raw.sessions) {
    $sessions = @($raw.sessions)
} elseif ($null -ne $raw) {
    $sessions = @($raw)
}

$durationsSec = [System.Collections.Generic.List[double]]::new()
$within3MinCount = 0
$validCount = 0
foreach ($s in $sessions) {
    if ($null -eq $s) { continue }
    $sec = 0.0
    if ($null -ne $s.first_broadcast_time_sec) {
        $sec = [double]$s.first_broadcast_time_sec
    } elseif ($null -ne $s.first_broadcast_ms) {
        $sec = [double]$s.first_broadcast_ms / 1000.0
    } else {
        continue
    }
    if ($sec -lt 0.0) { continue }
    $validCount++
    $durationsSec.Add($sec)
    if ($sec -le 180.0) {
        $within3MinCount++
    }
}

$observedRatePct = 0.0
if ($validCount -gt 0) {
    $observedRatePct = [Math]::Round((100.0 * $within3MinCount / $validCount), 2)
}
$p25 = [Math]::Round((Get-Percentile -Values @($durationsSec) -Percentile $TargetPercentile), 2)
$p50 = [Math]::Round((Get-Percentile -Values @($durationsSec) -Percentile 0.5), 2)
$p75 = [Math]::Round((Get-Percentile -Values @($durationsSec) -Percentile 0.75), 2)

$recommendedThresholdPct = [Math]::Max($FloorThresholdPct, [Math]::Floor($observedRatePct))
$recommendedMinSessionCount = [Math]::Max($MinSessionCountFloor, [Math]::Min(20, [int][Math]::Ceiling($validCount / 2.0)))

$result = [ordered]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    telemetry_path = $resolvedTelemetry
    valid_session_count = [int]$validCount
    within_3min_success_count = [int]$within3MinCount
    observed_within_3min_success_rate_pct = $observedRatePct
    p25_first_broadcast_sec = $p25
    p50_first_broadcast_sec = $p50
    p75_first_broadcast_sec = $p75
    floor_threshold_pct = $FloorThresholdPct
    percentile_probe = $TargetPercentile
    recommended_threshold_pct = $recommendedThresholdPct
    min_session_count_floor = $MinSessionCountFloor
    recommended_min_session_count = $recommendedMinSessionCount
}
$result | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedOutputJson -Encoding UTF8

$lines = @()
$lines += "Onboarding KPI Calibration Summary"
$lines += "Generated: $($result.generated_at_utc)"
$lines += "TelemetryPath: $resolvedTelemetry"
$lines += "ValidSessionCount: $validCount"
$lines += "Within3MinSuccessCount: $within3MinCount"
$lines += "ObservedWithin3MinSuccessRatePct: $observedRatePct"
$lines += "P25FirstBroadcastSec: $p25"
$lines += "P50FirstBroadcastSec: $p50"
$lines += "P75FirstBroadcastSec: $p75"
$lines += "RecommendedThresholdPct: $recommendedThresholdPct"
$lines += "RecommendedMinSessionCount: $recommendedMinSessionCount"
$lines | Set-Content -Path $resolvedOutputTxt -Encoding UTF8

Write-Host "summary=$resolvedOutputTxt"
Write-Host "json=$resolvedOutputJson"
