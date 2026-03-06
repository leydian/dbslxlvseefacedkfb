param(
    [string]$HistoryCsvPath = ".\build\reports\render_perf_gate_history.csv",
    [int]$LookbackRows = 10,
    [double]$MaxAllowedPrivateMbIncrease = 200.0,
    [double]$MaxAllowedWorkingSetMbIncrease = 250.0,
    [int]$MaxAllowedMemorySampleFailedCount = 0,
    [string]$SummaryPath = ".\build\reports\memory_trend_gate_summary.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedHistory = Resolve-AbsolutePath -Path $HistoryCsvPath -BaseDirectory $repoRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null

if (-not (Test-Path -LiteralPath $resolvedHistory)) {
    throw "render perf history csv missing: $resolvedHistory"
}

$rows = Import-Csv -Path $resolvedHistory
if ($rows.Count -lt 2) {
    throw "not enough history rows in $resolvedHistory (need >= 2)."
}

$window = @($rows | Select-Object -Last ([Math]::Max(2, $LookbackRows)))
$first = $window[0]
$last = $window[$window.Count - 1]

$firstPrivate = [double]$first.p95_private_mb
$lastPrivate = [double]$last.p95_private_mb
$firstWorking = [double]$first.p95_working_set_mb
$lastWorking = [double]$last.p95_working_set_mb
$lastMemorySampleFailedCount = [int]$last.memory_sample_failed_count

$privateIncrease = [Math]::Round(($lastPrivate - $firstPrivate), 3)
$workingIncrease = [Math]::Round(($lastWorking - $firstWorking), 3)

$gatePrivateTrend = $privateIncrease -le $MaxAllowedPrivateMbIncrease
$gateWorkingTrend = $workingIncrease -le $MaxAllowedWorkingSetMbIncrease
$gateMemoryFailed = $lastMemorySampleFailedCount -le $MaxAllowedMemorySampleFailedCount
$overall = $gatePrivateTrend -and $gateWorkingTrend -and $gateMemoryFailed

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Memory Trend Gate Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("HistoryCsv: $resolvedHistory")
$lines.Add("LookbackRowsUsed: $($window.Count)")
$lines.Add("FirstGeneratedUtc: $($first.generated_utc)")
$lines.Add("LastGeneratedUtc: $($last.generated_utc)")
$lines.Add("PrivateMbIncrease: $privateIncrease")
$lines.Add("WorkingSetMbIncrease: $workingIncrease")
$lines.Add("LastMemorySampleFailedCount: $lastMemorySampleFailedCount")
$lines.Add("")
$lines.Add("Gate Results")
$lines.Add("- GatePrivateTrend (increase <= $MaxAllowedPrivateMbIncrease): $(if ($gatePrivateTrend) { 'PASS' } else { 'FAIL' })")
$lines.Add("- GateWorkingTrend (increase <= $MaxAllowedWorkingSetMbIncrease): $(if ($gateWorkingTrend) { 'PASS' } else { 'FAIL' })")
$lines.Add("- GateMemorySampleFailed (last <= $MaxAllowedMemorySampleFailedCount): $(if ($gateMemoryFailed) { 'PASS' } else { 'FAIL' })")
$lines.Add("- Overall: $(if ($overall) { 'PASS' } else { 'FAIL' })")

$lines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"

if (-not $overall) {
    exit 1
}
