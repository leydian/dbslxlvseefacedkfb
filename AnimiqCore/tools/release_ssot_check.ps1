param(
    [string]$DashboardJsonPath = ".\build\reports\release_gate_dashboard.json",
    [string]$ReadinessSummaryPath = ".\build\reports\release_readiness_gate_summary.txt",
    [string]$OutputTxt = ".\build\reports\release_ssot_check_summary.txt",
    [string]$OutputJson = ".\build\reports\release_ssot_check_summary.json"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-KeyValueFromFile {
    param([string]$Path, [string]$Prefix)
    if (-not (Test-Path -LiteralPath $Path)) { return "" }
    $line = Get-Content -Path $Path | Where-Object { $_.StartsWith($Prefix) } | Select-Object -First 1
    if ($null -eq $line) { return "" }
    return $line.Substring($Prefix.Length).Trim()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedDashboardJsonPath = Resolve-AbsolutePath -Path $DashboardJsonPath -BaseDirectory $repoRoot
$resolvedReadinessSummaryPath = Resolve-AbsolutePath -Path $ReadinessSummaryPath -BaseDirectory $repoRoot
$resolvedOutputTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
$resolvedOutputJson = Resolve-AbsolutePath -Path $OutputJson -BaseDirectory $repoRoot

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputTxt) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutputJson) | Out-Null

if (-not (Test-Path -LiteralPath $resolvedDashboardJsonPath)) {
    throw "dashboard json missing: $resolvedDashboardJsonPath"
}
if (-not (Test-Path -LiteralPath $resolvedReadinessSummaryPath)) {
    throw "release readiness summary missing: $resolvedReadinessSummaryPath"
}

$dashboard = Get-Content -Raw -Path $resolvedDashboardJsonPath | ConvertFrom-Json
if ($null -eq $dashboard.gate_summary) {
    throw "dashboard json missing gate_summary."
}

$dashboardWpfOnly = if ([bool]$dashboard.gate_summary.release_candidate_wpf_only) { "PASS" } else { "FAIL" }
$dashboardFull = if ([bool]$dashboard.gate_summary.release_candidate_full) { "PASS" } else { "FAIL" }
$dashboardTracking = if ([bool]$dashboard.gate_summary.tracking_contract_all_pass) { "PASS" } else { "FAIL" }

$summaryWpfOnly = Get-KeyValueFromFile -Path $resolvedReadinessSummaryPath -Prefix "DashboardReleaseCandidateWpfOnly:"
$summaryFull = Get-KeyValueFromFile -Path $resolvedReadinessSummaryPath -Prefix "DashboardReleaseCandidateFull:"
$summaryTracking = Get-KeyValueFromFile -Path $resolvedReadinessSummaryPath -Prefix "DashboardTrackingContract:"

$matches = @(
    [PSCustomObject]@{
        key = "release_candidate_wpf_only"
        dashboard = $dashboardWpfOnly
        summary = $summaryWpfOnly
        match = [string]::Equals($dashboardWpfOnly, $summaryWpfOnly, [System.StringComparison]::OrdinalIgnoreCase)
    },
    [PSCustomObject]@{
        key = "release_candidate_full"
        dashboard = $dashboardFull
        summary = $summaryFull
        match = [string]::Equals($dashboardFull, $summaryFull, [System.StringComparison]::OrdinalIgnoreCase)
    },
    [PSCustomObject]@{
        key = "tracking_contract_all_pass"
        dashboard = $dashboardTracking
        summary = $summaryTracking
        match = [string]::Equals($dashboardTracking, $summaryTracking, [System.StringComparison]::OrdinalIgnoreCase)
    }
)

$overall = ($matches | Where-Object { -not $_.match }).Count -eq 0

$summaryObject = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
    dashboard_json_path = $resolvedDashboardJsonPath
    readiness_summary_path = $resolvedReadinessSummaryPath
    overall = if ($overall) { "PASS" } else { "FAIL" }
    checks = $matches
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Release SSOT Check Summary")
$lines.Add("GeneratedUTC: $($summaryObject.generated_utc)")
$lines.Add("DashboardJsonPath: $resolvedDashboardJsonPath")
$lines.Add("ReadinessSummaryPath: $resolvedReadinessSummaryPath")
$lines.Add("Overall: $($summaryObject.overall)")
$lines.Add("")
foreach ($row in $matches) {
    $lines.Add("- $($row.key): dashboard=$($row.dashboard), summary=$($row.summary), match=$($row.match)")
}

$lines | Set-Content -Path $resolvedOutputTxt -Encoding UTF8
$summaryObject | ConvertTo-Json -Depth 4 | Set-Content -Path $resolvedOutputJson -Encoding UTF8
Write-Host "summary_txt=$resolvedOutputTxt"
Write-Host "summary_json=$resolvedOutputJson"

if (-not $overall) {
    exit 1
}
